#include "client.h"

#include <unordered_set>

#include "hope-io/net/stream.h"
#include "hope-io/net/factory.h"
#include "message.h"
#include "hope-io/net/event_loop.h"

namespace {

    // so, as i can see i need to implement eventloop client
    // or some static method to peek chunk from stream as it done in read_chunk method
    class client_impl final : public ph::client {
    public:
        client_impl(std::string ip, int port)
                : m_host(std::move(ip)), m_port(port) {
            m_stream = hope::io::create_stream();
        }
        virtual plist_t list() override {
            ph::list_patches_request req;
            m_stream->connect(m_host, m_port);
            serialize(req);
            auto response = deserialize<ph::list_patches_response>();
            m_stream->disconnect();
            return response->patches;
        }
        virtual plist_t download(const std::string& tag) override {
            ph::get_patches_request req;
            req.tag = tag;
            m_stream->connect(m_host, m_port);
            serialize(req);
            auto response = deserialize<ph::get_patches_response>();
            m_stream->disconnect();
            return response->patches;
        }
        virtual plist_t upload(const plist_t& plist) override {
            ph::upload_patch_request request;
            request.patches = plist;
            m_stream->connect(m_host, m_port);
            serialize(request);
            auto response = deserialize<ph::upload_patch_response>();
            m_stream->disconnect();
            return response->patches;
        }
        virtual plist_t pdelete(const std::string& tag) override {
            ph::delete_patch_request request;
            request.tag = tag;
            m_stream->connect(m_host, m_port);
            serialize(request);
            auto response = deserialize<ph::delete_patch_response>();
            m_stream->disconnect();
            return response->removed_patches;
        }
    private:
        void serialize(ph::message& req) const {
            hope::io::event_loop::fixed_size_buffer b;
            bool complete = false;
            while (!complete) {
                ph::event_loop_stream_wrapper stream(b);
                complete = req.write(stream);
                const auto [dat, count] = b.used_chunk();
                m_stream->write(dat, count);
                b.reset();
            }
        }
        template<typename T>
        T* deserialize() const {
            hope::io::event_loop::fixed_size_buffer b;
            read_chunk(b);
            ph::event_loop_stream_wrapper stream(b);
            auto msg = ph::message::peek_response(stream);
            auto complete = false;
            while (!complete) {
                complete = msg->read(stream);
                if (!complete) {
                    read_chunk(b);
                }
            }
            return (T*)msg;
        }
        void read_chunk(hope::io::event_loop::fixed_size_buffer& b) const {
            b.reset();
            const auto size = m_stream->read<uint32_t>();
            b.write(&size, sizeof(size));
            const auto [dat, count] = b.free_chunk();
            if (size - sizeof(size) > 0) {
                m_stream->read((void*)dat, size - sizeof(size));
                b.handle_write(size - sizeof(size));
            }
        }
        hope::io::stream* m_stream{ nullptr };
        std::string m_host;
        int m_port{ 0 };
    };

}

ph::client* ph::client::create(const std::string& ip, int port) {
    return new client_impl(ip, port);
}