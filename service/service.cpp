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

#include "hope_thread/containers/queue/mpmc_bounded_queue.h"
#include "hope_thread/containers/queue/spsc_queue.h"
#include "hope_thread/runtime/threadpool.h"

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
        service_impl()
        {
            constexpr auto port = 1555;
            m_event_loop = hope::io::create_event_loop();
            LOG(INFO) << "Created event loop";
            LOG(INFO) << "Run loop on port" << HOPE_VAL(port);

            hope::io::event_loop::config ev_cfg;
            ev_cfg.port = port;
            m_event_loop->run(ev_cfg, 
            hope::io::event_loop::callbacks {
                [this] (auto&& c) {
                    on_create(c);
                },
                [this] (auto&& c) {
                    on_read(c);
                },
                [this] (auto&& c) {
                    on_write(c);
                },
                [this] (hope::io::event_loop::connection& c, const std::string& err) {
                    on_error(c, err);
                }
            });    
        }

        virtual ~service_impl() override {
            m_running.store(false, std::memory_order_release);
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
                event_loop_stream_wrapper stream(*c.buffer);
                stream.begin_write();
                const auto complete = msg_ptr->write(stream);
                stream.end_write();
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

        void handle_request(event_loop_stream_wrapper& stream, hope::io::event_loop::connection& c, auto state_iterator, message* msg) {
            stream.begin_read();
            const auto complete = msg->read(stream);
            stream.end_read();
            if (complete) {
                LOG(INFO) << "Message fully read";
                c.set_state(hope::io::event_loop::connection_state::write);
            } // otherwise needs more reads
        }

        std::atomic<bool> m_running{ true };
        hope::io::event_loop* m_event_loop{ nullptr };

        // client id (raw socket) to client state
        std::unordered_map<int32_t, message*> m_active_clients;
        // revision and platform is a key for patches
        using patch_key_t = std::pair<revision_t, std::string>;
        using patch_array_t = std::vector<std::shared_ptr<patch>>;

        std::unordered_map<patch_key_t, patch_array_t> patch_registry;
    };

    service* create_service() {
        return new service_impl();
    }
}