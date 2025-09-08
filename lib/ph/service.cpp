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
            };
        }
        virtual void run() override {
            constexpr auto port = 1555;
            m_event_loop = hope::io::create_event_loop();
            LOG(INFO) << "Created event loop";
            LOG(INFO) << "Run loop on port" << HOPE_VAL(port);
            hope::io::event_loop::config ev_cfg;
            ev_cfg.port = port;
            m_event_loop->run(ev_cfg,
            hope::io::event_loop::callbacks {
                [this] (auto&& c) { on_create(c); },
                [this] (auto&& c) { on_read(c); },
                [this] (auto&& c) { on_write(c); },
                [this] (hope::io::event_loop::connection& c, const std::string& err) { on_error(c, err); }
            });
        }
        virtual ~service_impl() override {
            m_event_loop->stop();
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
    };

    service* create_service() {
        return new service_impl();
    }
}