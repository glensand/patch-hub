#include "client.h"

#include "hope-io/net/stream.h"
#include "hope-io/net/factory.h"
#include "message.h"
#include "hope-io/net/event_loop.h"

namespace {

    // so, as i can see i need to implement eventloop client
    // or some static method to peek chunk from stream as it done in read_chunk method
    class client_impl final : public ph::client {
    public:
        client_impl(const std::string& ip, int port)
                : m_host(ip), m_port(port) {
            m_stream = hope::io::create_stream();
        }
        virtual plist_t list() override {
            ph::list_patches_request req;
            m_stream->connect(m_host, m_port);
            serialize(req);
            auto response = deserialize<ph::list_patches_response>();
            m_stream->disconnect();
            plist_t res;
            for (const auto& p : response->patches) {
                patch newp;
                newp.name = p.patch_name;
                newp.platform = p.platform;
                newp.file_size = p.size;
                newp.revision = p.revision;
                res.push_back(std::move(newp));
            }
            return res;
        }
        virtual plist_t download(std::size_t revision, const std::string& platform) override {
            ph::get_patches_request req;
            req.revision = revision;
            req.platform = platform;
            m_stream->connect(m_host, m_port);
            serialize(req);
            auto response = deserialize<ph::get_patches_response>();
            m_stream->disconnect();
            plist_t res;
            for (const auto& p : response->patches) {
                patch newp;
                newp.name = p->name;
                newp.platform = platform;
                newp.file_size = p->file_size;
                newp.revision = revision;
                newp.data = p->data;
                p->data = nullptr;
                res.emplace_back(std::move(newp));
            }
            return res;
        }
        virtual plist_t upload(const plist_t& plist) override {
            ph::upload_patch_request request;
            for (const auto& p : plist) {
                request.platform = p.platform;
                request.revision = p.revision;
                auto patch = std::make_shared<ph::patch>();
                patch->name = p.name;
                patch->file_size = p.file_size;
                patch->data = p.data;
                request.patches.emplace_back(std::move(patch));
            }
            m_stream->connect(m_host, m_port);
            serialize(request);
            auto response = deserialize<ph::upload_patch_response>();
            m_stream->disconnect();
            plist_t res;
            for (const auto& p : response->patches) {
                patch newp;
                newp.name = p.name;
                newp.file_size = p.file_size;
                res.emplace_back(std::move(newp));
            }
            return res;
        }
        virtual plist_t pdelete(std::size_t revision, const std::string& platform) override {
            ph::delete_patch_request request;
            request.platform = platform;
            request.revision = revision;
            m_stream->connect(m_host, m_port);
            serialize(request);
            auto response = deserialize<ph::delete_patch_response>();
            m_stream->disconnect();
            plist_t res;
            for (const auto& p : response->removed_patches) {
                patch newp;
                newp.name = p.patch_name;
                newp.file_size = p.size;
                res.emplace_back(std::move(newp));
            }
            return res;
        }
    private:
        void serialize(ph::message& req) const {
            ph::list_patches_request request;
            hope::io::event_loop::fixed_size_buffer b;
            bool complete = false;
            while (!complete) {
                ph::event_loop_stream_wrapper stream(b);
                stream.begin_write();
                complete = req.write(stream);
                stream.end_write();
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
            stream.begin_read();
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
            m_stream->read((void*)dat, size);
            b.handle_write(size);
        }
        hope::io::stream* m_stream{ nullptr };
        std::string m_host;
        int m_port{ 0 };
    };

}

ph::client* ph::client::create(const std::string& ip, int port) {
    return new client_impl(ip, port);
}