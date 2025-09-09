#include "service.h"

#include <atomic>
#include <cstdlib>
#include <thread>
#include <iostream>
#include <unordered_map>
#include <array>
#include <vector>
#include <fstream>
#include <memory>
#include <filesystem>

#include "hope-io/net/stream.h"
#include "hope-io/net/event_loop.h"
#include "hope-io/net/factory.h"

#include "hope_logger/logger.h"

#include "stream_wrapper.h"
#include "message.h"
#include "hope_thread/containers/queue/spsc_queue.h"
#include "hope_thread/runtime/worker_thread.h"

namespace ph {

    class service_impl final : public service {
        using buffer_t = hope::io::event_loop::fixed_size_buffer;
    public:
        using clients_t = std::unordered_map<int32_t, message*>;
        using state_t = clients_t::iterator;
        using exec_t = std::function<void(event_loop_stream_wrapper& stream, hope::io::event_loop::connection& c,
            state_t in_state, message* msg)>;

        virtual void stop() override {
            m_event_loop->stop();
        }
        service_impl()
        {
            m_exec[(uint8_t)message::etype::list_patches] = [&]
                (event_loop_stream_wrapper& stream, hope::io::event_loop::connection& c,
                    state_t in_state, message* msg) {
                delete msg;
                LOG(INFO) << "Got list message" << HOPE_VAL(c.descriptor);
                list_patches_response response;
                for (const auto& [_, array] : m_patch_registry) {
                    for (const auto& p : array) {
                        response.patches.emplace_back(p);
                    }
                }
                // 8kb inside buffer should be enough to write all registered stuff (i hope)
                response.write(stream);
                in_state->second = nullptr;
                c.set_state(hope::io::event_loop::connection_state::write);
            };
            m_exec[uint8_t((uint8_t)message::etype::upload_patch)] = [&](event_loop_stream_wrapper& stream,
                hope::io::event_loop::connection& c, state_t in_state, message* msg) {
                const auto request = static_cast<upload_patch_request*>(msg);
                LOG(INFO) << "Got upload message" << HOPE_VAL(c.descriptor);
                for (const auto& p : request->patches) {
                    LOG(INFO) << HOPE_VAL(p->name) << HOPE_VAL(p->file_size) << HOPE_VAL(p->platform) << HOPE_VAL(p->revision);
                    const patch_key key{ p->revision, p->platform };
                    auto& entry = m_patch_registry[key];
                    bool replaced = false;
                    for (auto& maybepatch : entry) {
                        if (maybepatch->name == p->name) {
                            maybepatch = p;
                            replaced = true;
                        }
                    }
                    if (!replaced) {
                        entry.emplace_back(p);
                    }
                }
                upload_patch_response response;
                // send names and meta back, so the client be sure everethyng is ok
                response.patches = request->patches;
                delete msg;
                // 8kb inside buffer should be enough to write all registered stuff (i hope)
                response.write(stream);
                in_state->second = nullptr;
                c.set_state(hope::io::event_loop::connection_state::write);
                m_io_cmd.enqueue([this, patches = response.patches] {
                    cput(patches);
                });
            };
            m_exec[uint8_t(message::etype::get_patches)] = [&](event_loop_stream_wrapper& stream,
                hope::io::event_loop::connection& c, state_t in_state, message* msg) {
                const auto get_patches_request = static_cast<ph::get_patches_request*>(msg);
                LOG(INFO) << "Got patch request" << HOPE_VAL(c.descriptor) << HOPE_VAL(get_patches_request->platform) << HOPE_VAL(get_patches_request->revision);
                const auto revision = get_patches_request->revision;
                const auto platform = get_patches_request->platform;
                const patch_key key{ revision, platform };
                auto entry = m_patch_registry[key];
                auto* response = new get_patches_response;
                response->patches = entry;
                for (const auto& p : response->patches) {
                    LOG(INFO) << "Found patch:" << HOPE_VAL(p->name) << HOPE_VAL(p->file_size) << HOPE_VAL(p->platform) << HOPE_VAL(p->revision);
                }
                delete msg;
                // if complete remove msg right now
                if (response->write(stream)) {
                    delete response;
                    in_state->second = nullptr;
                } else {
                    in_state->second = response;
                }
                c.set_state(hope::io::event_loop::connection_state::write);
            };
            m_exec[uint8_t(message::etype::delete_patch)] = [&](event_loop_stream_wrapper& stream,
                hope::io::event_loop::connection& c, state_t in_state, message* msg) {
                const auto delete_patch = static_cast<delete_patch_request*>(msg);
                LOG(INFO) << "Delete patch request" << HOPE_VAL(c.descriptor) << HOPE_VAL(delete_patch->platform) << HOPE_VAL(delete_patch->revision);
                const auto platform = delete_patch->platform;
                const auto revision = delete_patch->revision;
                const patch_key key{ revision, platform };
                delete_patch_response response;
                const auto& entry = m_patch_registry.find(key);
                if (entry != m_patch_registry.end()) {
                    response.removed_patches = entry->second;
                    for (const auto& p : response.removed_patches) {
                        LOG(INFO) << "Removed patch:" << HOPE_VAL(p->name) << HOPE_VAL(p->file_size) << HOPE_VAL(p->platform) << HOPE_VAL(p->revision);
                    }
                    m_patch_registry.erase(entry);
                }
                response.write(stream);
                delete msg;
                in_state->second = nullptr;
                c.set_state(hope::io::event_loop::connection_state::write);
                m_io_cmd.enqueue([this, patches = response.removed_patches] {
                    cdelete(patches);
                });
            };
            restore_from_cache();
            m_running = true;
            m_io = std::thread([this] {
	            io();
            });
        }
        virtual void run(int port) override {
            m_event_loop = hope::io::create_event_loop();
            LOG(INFO) << "Created event loop";
            LOG(INFO) << "Run loop on port" << HOPE_VAL(port);
            hope::io::event_loop::config ev_cfg;
            ev_cfg.port = port;
            try {
                m_event_loop->run(ev_cfg,
                    hope::io::event_loop::callbacks {
                    [this] (auto&& c) { on_create(c); },
                    [this] (auto&& c) { on_read(c); },
                    [this] (auto&& c) { on_write(c); },
                    [this] (auto&& c, auto&& err) { on_error(c, err); }
                });
            } catch (const std::exception& ex) {
                LOG(LERR) << HOPE_VAL(ex.what());
            }
        }
        virtual ~service_impl() override {
            m_running.store(false);
            m_io.join();
            // clear queue for sure
            std::function<void()> f;
            while (m_io_cmd.try_dequeue(f)) {
                f();
            }
            delete m_event_loop;
        }

    private:
        void on_create(hope::io::event_loop::connection& c) {
            // TODO:: add ip address to connection, or add method to resolve desriptor
            LOG(INFO) << "Created connection" << HOPE_VAL(c.descriptor);
            c.set_state(hope::io::event_loop::connection_state::read);
        }

        void on_read(hope::io::event_loop::connection& c) {
            event_loop_stream_wrapper stream(*c.buffer);
            if (stream.is_ready_to_read()) {
                if (auto state = m_active_clients.find(c.descriptor); state != end(m_active_clients)) {
                    auto* msg_ptr = state->second;
                    LOG(INFO) << "Got new chunk for message"
                        << HOPE_VAL(message::str_type(msg_ptr->get_type()));
                    handle_request(stream, c, state, msg_ptr);
                } else {
                    auto* new_message = message::peek_request(stream);
                    state = m_active_clients.emplace(c.descriptor, new_message).first;
                    handle_request(stream, c, state, new_message);
                }
            }
        }

        void on_write(hope::io::event_loop::connection& c) {
            if (auto state = m_active_clients.find(c.descriptor); state != end(m_active_clients)) {
                auto* msg_ptr = state->second;
                bool complete = msg_ptr == nullptr;
                if (!complete) {
                    event_loop_stream_wrapper stream(*c.buffer);
                    complete = msg_ptr->write(stream);
                }
                if (complete) {
                    LOG(INFO) << "Send last chunk for msg, close connection" << HOPE_VAL(c.descriptor);
                    delete msg_ptr;
                    m_active_clients.erase(state);
                    c.set_state(hope::io::event_loop::connection_state::die);
                }
            } else {
                LOG(INFO) << "Cannot find active state for client, kill connection" << HOPE_VAL(c.descriptor);
                c.set_state(hope::io::event_loop::connection_state::die);
            }
        }

        void on_error(hope::io::event_loop::connection& c, const std::string& err) {
            LOG(INFO) << "Fatal error" << HOPE_VAL(err);
            if (auto active_message = m_active_clients.find(c.descriptor); active_message != end(m_active_clients)) {
                delete active_message->second;
                m_active_clients.erase(active_message);
            }
        }

        void handle_request(event_loop_stream_wrapper& stream, hope::io::event_loop::connection& c, state_t state_iterator, message* msg) {
	        if (const auto complete = msg->read(stream)) {
                LOG(INFO) << "Message fully read";
                m_exec[(int8_t)msg->get_type()](stream, c, state_iterator, msg);
            } // otherwise needs more reads
        }

        void io() {
            while (m_running.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100)); // most stable stuff ever
                std::function<void()> f;
                while (m_io_cmd.try_dequeue(f)) {
	                f();
                }
            }
        }

        void restore_from_cache() {
            LOG(INFO) << "Restore from cache";
            std::filesystem::path p = m_cache_dir;
            try {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(p)) {
                    if (entry.is_regular_file()) {
                        const auto new_p = entry.path().string();
                        const auto filename = entry.path().filename().string();
                        // /cache/platform_revision/
                        auto parent_path = entry.path().parent_path().string();
                        auto platform_revision = std::string(parent_path.c_str() + 6, parent_path.size() - 6);
                        const auto pos = platform_revision.rfind('_');
                        if (pos != std::string::npos) {
                            std::string platform = platform_revision.substr(0, pos);
                            std::string revision = platform_revision.substr(pos + 1);
                            LOG(INFO) << "Trying to restore patch" << HOPE_VAL(platform) << HOPE_VAL(revision) << HOPE_VAL(filename);
                            const auto revision_int = std::stoi(revision);
                            std::ifstream file(new_p, std::ios::binary | std::ios::ate);
                            if (file.is_open()) {
                                const auto size = file.tellg();
                                file.seekg(0, std::ios::beg);
                                auto new_patch = std::make_shared<patch>();
                                new_patch->file_size = (uint32_t)size;
                                new_patch->name = filename;
                                new_patch->platform = platform;
                                new_patch->data = new uint8_t[size];
                                new_patch->revision = revision_int;
                                if (!file.read((char*)new_patch->data, size)) {
                                    LOG(LERR) << "Cannot read file" << HOPE_VAL(new_p);
                                } else {
                                    const patch_key key{ revision_t(revision_int), platform };
                                    auto& registry_entry = m_patch_registry[key];
                                    registry_entry.emplace_back(std::move(new_patch));
                                }
                            } else {
	                            LOG(LERR) << "Cannot open file" << HOPE_VAL(new_p);
                            }
                        } else {
                            LOG(LERR) << "Cannot parse cache" << HOPE_VAL(new_p);
                        }
                    }
                }
            }
            catch (const std::filesystem::filesystem_error& e) {
                LOG(INFO) << "Cache load err" << HOPE_VAL(e.what());
            }
            catch (const std::exception& e) {
                LOG(INFO) << "Cache load err" << HOPE_VAL(e.what());
            }
            catch (...){
                LOG(INFO) << "Cache load err unknown";
            }
            for (const auto& [k, patches] : m_patch_registry) {
                LOG(INFO) << "Loaded patches for" << HOPE_VAL(k.platform) << HOPE_VAL(k.revision);
                for (const auto& p : patches) {
                    LOG(INFO) << HOPE_VAL(p->name);
                }
            }
        }

        void cput(const std::vector<std::shared_ptr<patch>>& patches) {
            cdelete(patches);
	        for (const auto& p : patches) {
		        const auto subdir = m_cache_dir + p->platform + "_" + std::to_string(p->revision) + "/";
                const auto path = subdir + p->name;
                LOG(INFO) << "Put patch to cache" << HOPE_VAL(path);
                try {
                    if (std::filesystem::create_directories(subdir)) {
                        LOG(INFO) << "Created dir tree for patch cache" << HOPE_VAL(subdir);
                    }
                }
                catch (const std::filesystem::filesystem_error& e) {
                    LOG(INFO) << "Crete folder err" << HOPE_VAL(e.what());
                }
                std::ofstream cache(path);
                if (cache.is_open()) {
	                cache.write((char*)p->data, p->file_size);
                    LOG(INFO) << "Patch preserver successfully" << HOPE_VAL(path);
                } else {
                    LOG(INFO) << "Cannot open file" << HOPE_VAL(path);
                }
	        }
        }

        void cdelete(const std::vector<std::shared_ptr<patch>>& patches) {
            for (const auto& p : patches) {
                const auto subdir = m_cache_dir + "/" + p->platform + "_" + std::to_string(p->revision) + "/";
                const auto path = subdir + p->name;
                try {
                    if (std::filesystem::remove(path)) {
                        LOG(INFO) << "Removed old patch from cache" << HOPE_VAL(path);
                    }
                }
                catch (const std::filesystem::filesystem_error& e) {
                    LOG(INFO) << "Cannot remove patch (not always an error)" << HOPE_VAL(e.what());
                }
            }
        }

        std::atomic_bool m_running;
        hope::io::event_loop* m_event_loop{ nullptr };

        // client id (raw socket) to client state
        std::unordered_map<int32_t, message*> m_active_clients;
        std::array<exec_t, (int8_t)message::etype::count> m_exec;

        // revision and platform is a key for patches
        struct patch_key final {
            revision_t revision{};
            std::string platform;
            bool operator==(const patch_key& other) const {
                return revision == other.revision && platform == other.platform;
            }
            struct hash final {
                std::size_t operator()(const patch_key& k) const {
                    std::size_t h1 = std::hash<revision_t>{}(k.revision);
                    std::size_t h2 = std::hash<std::string>{}(k.platform);
                    return h1 ^ (h2 << 1);
                }
            };
        };
        using patch_array_t = std::vector<std::shared_ptr<patch>>;
        std::unordered_map<patch_key, patch_array_t, patch_key::hash> m_patch_registry;
        hope::threading::spsc_queue<std::function<void()>> m_io_cmd;
        std::thread m_io;
        const std::string m_cache_dir = "cache/";
    };

    service* create_service() {
        return new service_impl();
    }
}
