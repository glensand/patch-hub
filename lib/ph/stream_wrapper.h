/* Copyright (C) 2025 Gleb Bezborodov - All Rights Reserved
* You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/patch-hub
 */

#pragma once

#include "hope-io/net/event_loop.h"

namespace ph {
    struct event_loop_stream_wrapper final {
        enum class estate : uint8_t{
            read,
            write,
            none,
        };
        explicit event_loop_stream_wrapper(hope::io::event_loop::fixed_size_buffer& in_buffer)
            : buffer(in_buffer) { }

        [[nodiscard]] bool is_ready_to_read() const {
            if (buffer.count() > sizeof(uint32_t)) {
                const auto used_chunk = buffer.used_chunk();
                const auto message_length = *(uint32_t*)(used_chunk.first);
                return message_length == used_chunk.second;
            }
            return false;
        }
        void write(const void *data, std::size_t length) const {
            begin_write();
            buffer.write(data, length);
            const auto used_chunk = buffer.used_chunk();
            *(uint32_t*)used_chunk.first = (uint32_t)used_chunk.second;  // NOLINT(clang-diagnostic-cast-qual)
        }
        size_t read(void *data, std::size_t length) const {
            begin_read();
            assert(state == estate::read);
            return buffer.read(data, length);
        }

        auto free_space() const noexcept { return buffer.free_space(); }
        auto count() const noexcept { return buffer.count(); }
        template<typename TValue>
        void write(const TValue &val) {
            static_assert(std::is_trivial_v<std::decay_t<TValue>>,
                          "write(const TValue&) is only available for trivial types");
            write(&val, sizeof(val));
        }
        template<typename TValue>
        void read(TValue& val) {
            static_assert(std::is_trivial_v <std::decay_t<TValue>> ,
                          "read() is only available for trivial types");
            read(&val, sizeof(val));
        }
        template<typename TValue>
        TValue read() {
            TValue val;
            read(val);
            return val;
        }

    private:
        void begin_write() const {
            if (state != estate::write) {
                buffer.reset();
                // seek buffer to 4 bytes, for event-loop it is important to know count of bytes we'll receive at this stage
                buffer.handle_write(sizeof(uint32_t));
            }
            state = estate::write;
        }
        void begin_read() const {
            if (state != estate::read) {
                // skip first 4 bytes, belongs to loop wrapper
                buffer.handle_read(sizeof(uint32_t));
            }
            state = estate::read;
        }
        mutable estate state = estate::none;
        hope::io::event_loop::fixed_size_buffer& buffer;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    };

    template <>
    inline void event_loop_stream_wrapper::read<std::string>(std::string& val) {
        const auto size = read<uint16_t>();
        if (size > 0) {
            val.resize(size);
            read(val.data(), size);
        }
    }
    template <>
    inline void event_loop_stream_wrapper::write<std::string>(const std::string& val) {
        write((uint16_t)val.size());
        write(val.c_str(), val.size());
    }
}
