/* Copyright (C) 2025 Gleb Bezborodov - All Rights Reserved
* You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/patch-hub
 */

#pragma once

#include <string>
#include "stream_wrapper.h"
#include "service.h"
#include <cassert>

namespace ph {

    // flow:
    // client : message -> server
    // server : set state (streaming/receiving/answer/doaction+answer)
    struct message {
        enum class etype : uint8_t {
            list_patches,
            upload_patch,
            delete_patch,
            get_patches,
            count,
        };
        static const std::string& str_type(const etype type) {
            switch (type) {
                case etype::list_patches: static std::string s("list_patches"); return s;
                case etype::delete_patch: static std::string s2("delete_patch"); return s2;
                case etype::get_patches: static std::string s3("get_patches"); return s3;
                case etype::upload_patch: static std::string s4("upload_patch"); return s4;
                default: ;
            }
            static std::string su("unknown");
            return su;
        }

        message(etype in_type) : type(in_type) {}
        virtual ~message() = default;

        // writes part of data to buffer, returns true on complete
        // false if more writes is needed
        bool write(event_loop_stream_wrapper& stream) {
            if (initial) {
                stream.write(type);
                initial = false;
            }
            return write_impl(stream);
        }
        // reads part of data from buffer, returns true on complete
        // false if more reads is needed
        bool read(event_loop_stream_wrapper& stream) {
            return read_impl(stream);
        }

        etype get_type() const noexcept { return type; }

        // construct message from stream buffer, do not read anything from it (except 1 byte:msg type)
        static message* peek_response(event_loop_stream_wrapper& stream);
        static message* peek_request(event_loop_stream_wrapper& stream);
    protected:
        virtual bool write_impl(event_loop_stream_wrapper& stream){ return true; }
        virtual bool read_impl(event_loop_stream_wrapper& stream){ return true; }

    private:
        etype type{};
        bool initial = true;
    };

    struct patch_message : message {
        // here we use sharedptr 'cause the data could be swapped at any time
        std::vector<std::shared_ptr<patch>> patches;
        std::string platform{};
        revision_t revision{};
    protected:
        explicit patch_message(etype in_type) : message(in_type) { }
    private:
        virtual bool write_impl(event_loop_stream_wrapper& stream) override {
            assert(!platform.empty());
            assert(revision != 0);
            if (remaining_count == 0) {
                stream.write(platform);
                stream.write(revision);
                stream.write((uint16_t)patches.size());
                for (const auto& patch : patches) {
                    stream.write(patch->name);
                    stream.write((uint32_t)patch->file_size);
                    remaining_count += patch->file_size;
                }
            }
            return do_stream_action(
            [&stream](const uint8_t* begin, std::size_t size) {
                stream.write(begin, size);
            },
    [&stream] {
                return stream.free_space();
            });
        }
        virtual bool read_impl(event_loop_stream_wrapper& stream) override {
            if (remaining_count == 0) {
                stream.read(platform);
                stream.read(revision);
                const auto patch_count = stream.read<uint16_t>();
                for (auto i = 0; i < patch_count; i++) {
                    auto patch_ptr = std::make_shared<patch>();
                    stream.read(patch_ptr->name);
                    patch_ptr->file_size = stream.read<uint32_t>();
                    patch_ptr->data = new uint8_t[patch_ptr->file_size];
                    remaining_count += patch_ptr->file_size;
                    patches.push_back(std::move(patch_ptr));
                }
            }
            return do_stream_action(
            [&stream](uint8_t* begin, std::size_t size) {
                stream.read(begin, size);
            },
            [&stream] {
                return stream.count();
            });
        }
        bool do_stream_action(auto stream_action, auto get_count) {
            auto count = get_count();
            for (auto patch = patch_id; patch < patches.size() && count > 0; patch++) {
                const auto patch_size = patches[patch_id]->file_size;
                const auto begin = patches[patch_id]->data + current_patch_offset;
                const auto size = patch_size - current_patch_offset;
                stream_action(begin, size);
                current_patch_offset += size;
                remaining_count -= size;
                if (current_patch_offset == patch_size) {
                    current_patch_offset = 0;
                    ++patch_id;
                }
            }
            return remaining_count == 0;
        }
        // dynamic data
        std::size_t remaining_count = 0;
        std::size_t current_patch_offset = 0;
        std::size_t patch_id = 0;
    };

    // client -> server request patches for specified revision
    struct get_patches_request final : message {
        get_patches_request() : message(etype::get_patches){}
        revision_t revision{};
        std::string platform{};
    private:
        virtual bool write_impl(event_loop_stream_wrapper& stream) override {
            assert(!platform.empty());
            assert(revision != 0);
            stream.write(revision);
            stream.write(platform);
            return true;
        }
        virtual bool read_impl(event_loop_stream_wrapper& stream) override {
            stream.read(revision);
            stream.read(platform);
            return true;
        }
    };

    struct get_patches_response final : patch_message {
        get_patches_response() : patch_message(etype::get_patches){}
    };

    // client -> server message to store patches for specified revision and platform
    struct upload_patch_request final : patch_message {
        upload_patch_request() : patch_message(etype::upload_patch) {}
    };

    struct upload_patch_response final : message {
        struct uploaded_patch final {
            std::string name;
            std::size_t file_size{};
        };
        std::vector<uploaded_patch> patches;
        upload_patch_response() : message(etype::upload_patch){}
    private:
        virtual bool write_impl(event_loop_stream_wrapper& stream) override {
            stream.write((uint16_t)patches.size());
            for (const auto& [name, size] : patches) {
                stream.write(name);
                stream.write(size);
            }
            return true;
        }
        virtual bool read_impl(event_loop_stream_wrapper& stream) override {
            const auto num = stream.read<uint16_t>();
            for (auto i = 0; i < num; i++) {
                uploaded_patch patch;
                stream.read(patch.name);
                stream.read(patch.file_size);
                patches.push_back(std::move(patch));
            }
            return true;
        }
    };

    // client -> server request list of available patches
    struct list_patches_request final : message {
        list_patches_request() : message(etype::list_patches){}
    };

    struct list_patches_response final : message {
        list_patches_response() : message(etype::list_patches){}
        struct patch_with_revision final {
            std::string patch_name;
            std::string platform;
            revision_t revision{ };
            std::size_t size{ };
        };
        std::vector<patch_with_revision> patches;
    private:
        virtual bool write_impl(event_loop_stream_wrapper& stream) override {
            stream.write((uint16_t)patches.size());
            for (const auto& [name, platform, revision, size] : patches) {
                stream.write(name);
                stream.write(revision);
                stream.write(platform);
                stream.write(size);
            }
            return true;
        }
        virtual bool read_impl(event_loop_stream_wrapper& stream) override {
            const auto num = stream.read<uint16_t>();
            for (auto i = 0; i < num; i++) {
                patch_with_revision patch;
                stream.read(patch.patch_name);
                stream.read(patch.revision);
                stream.read(patch.platform);
                stream.read(patch.size);
                patches.push_back(std::move(patch));
            }
            return true;
        }
    };

    struct delete_patch_request final : message {
        delete_patch_request() : message(etype::delete_patch){}
        revision_t revision{};
        std::string platform{};
    private:
        virtual bool write_impl(event_loop_stream_wrapper& stream) override {
            stream.write(revision);
            stream.write(platform);
            return true;
        }
        virtual bool read_impl(event_loop_stream_wrapper& stream) override {
            stream.read(revision);
            stream.read(platform);
            return true;
        }
    };

    struct delete_patch_response final : message {
        struct removed_patch final {
            std::string patch_name;
            std::size_t size{};
        };
        std::vector<removed_patch> removed_patches;
        delete_patch_response() : message(etype::delete_patch){}
    private:
        virtual bool write_impl(event_loop_stream_wrapper& stream) override {
            stream.write((uint16_t)removed_patches.size());
            for (const auto&[name, size] : removed_patches) {
                stream.write(name);
                stream.write(size);
            }
            return true;
        }
        virtual bool read_impl(event_loop_stream_wrapper& stream) override {
            const auto num = stream.read<uint16_t>();
            for (auto i = 0; i < num; i++) {
                removed_patch patch;
                stream.read(patch.patch_name);
                stream.read(patch.size);
                removed_patches.push_back(std::move(patch));
            }
            return true;
        }
    };

    inline
    message* message::peek_request(event_loop_stream_wrapper &stream) {
        switch (const auto type = stream.read<etype>()) {
            case etype::delete_patch: return new delete_patch_request();
            case etype::upload_patch: return new upload_patch_request();
            case etype::list_patches: return new list_patches_request();
            case etype::get_patches: return new get_patches_request();
            default: ;
        }
        return nullptr;
    }

    inline
    message* message::peek_response(event_loop_stream_wrapper &stream) {
        switch (const auto type = stream.read<etype>()) {
            case etype::list_patches: return new list_patches_response();
            case etype::upload_patch: return new upload_patch_response();
            case etype::delete_patch: return new delete_patch_response();
            case etype::get_patches: return new get_patches_response();
            default: ;
        }
        return nullptr;
    }

}