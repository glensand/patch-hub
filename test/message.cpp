#include <cassert>

#include "hope-io/net/event_loop.h"
#include "ph/message.h"

void serialize_list_request() {
    ph::list_patches_request request;
    hope::io::event_loop::fixed_size_buffer b;
    ph::event_loop_stream_wrapper stream(b);

    request.write(stream);
    auto request_deserialized = ph::message::peek_request(stream);
    request_deserialized->read(stream);

    assert(request_deserialized->get_type() == request.get_type());
}

void serialize_list_response() {
    ph::list_patches_response response;
    for (auto i = 0; i < 5; ++i) {
        auto p = std::make_shared<ph::patch>();
        p->platform = "WindowsClient";
        p->name = "random_patch_name" + std::to_string(i);
        p->revision = i;
        p->file_size = i * 100000;
        response.patches.push_back(std::move(p));
    }
    hope::io::event_loop::fixed_size_buffer b;
    ph::event_loop_stream_wrapper stream(b);
    response.write(stream);

    auto response_deserialized = ph::message::peek_response(stream);
    response_deserialized->read(stream);

    assert(response_deserialized->get_type() == response.get_type());
    const auto list_response = static_cast<ph::list_patches_response *>(response_deserialized);
    for (auto i = 0; i < response.patches.size(); ++i) {
        assert(list_response->patches[i]->name == response.patches[i]->name);
        assert(list_response->patches[i]->platform == response.patches[i]->platform);
        assert(list_response->patches[i]->revision == response.patches[i]->revision);
        assert(list_response->patches[i]->file_size == response.patches[i]->file_size);
    }
}

void serialize_delete_request() {
    ph::delete_patch_request request;
    request.platform = "WindowsClient";
    request.revision = 1;
    hope::io::event_loop::fixed_size_buffer b;
    ph::event_loop_stream_wrapper stream(b);

    request.write(stream);

    auto request_deserialized = ph::message::peek_request(stream);
    request_deserialized->read(stream);
    assert(request_deserialized->get_type() == request.get_type());
    const auto delete_request = static_cast<ph::delete_patch_request *>(request_deserialized);
    assert(delete_request->revision == request.revision);
    assert(delete_request->platform == request.platform);
}

void serialize_delete_response() {
    ph::delete_patch_response response;
    for (auto i = 0; i < 5; ++i) {
		auto p = std::make_shared<ph::patch>();
        p->name = "random_name" + std::to_string(i);
        p->file_size = i * 100000;
    }
    hope::io::event_loop::fixed_size_buffer b;
    ph::event_loop_stream_wrapper stream(b);
    response.write(stream);

    auto response_deserialized = ph::message::peek_response(stream);
    response_deserialized->read(stream);

    assert(response_deserialized->get_type() == response.get_type());
    const auto delete_response = static_cast<ph::delete_patch_response *>(response_deserialized);
    for (auto i = 0; i < response.removed_patches.size(); ++i) {
        assert(delete_response->removed_patches[i]->name == response.removed_patches[i]->name);
        assert(delete_response->removed_patches[i]->file_size == response.removed_patches[i]->file_size);
    }
}

void serialize_upload_request() {
    constexpr static auto buffer_size = 32 * 1024;
    auto* test_buffer = new uint8_t[buffer_size]; // 32k is good
    for (auto i = 0; i < buffer_size; ++i) {
        test_buffer[i] = std::rand() % 256;
    }
    ph::upload_patch_request request;
    for (auto i= 0 ; i < 5; ++i) {
        auto testp = std::make_shared<ph::patch>();
        testp->platform = "WindowsClient";
        testp->revision = 1;
        testp->name = "random_name" + std::to_string(i);
        testp->file_size = std::rand() % buffer_size;
        testp->data = test_buffer;
        request.patches.emplace_back(std::move(testp));
    }
    hope::io::event_loop::fixed_size_buffer b;
    ph::event_loop_stream_wrapper stream(b);
    
    auto complete = request.write(stream);
    auto request_deserialized = ph::message::peek_request(stream);
    request_deserialized->read(stream);

    auto upload_request = static_cast<ph::upload_patch_request *>(request_deserialized);
    assert(request_deserialized->get_type() == request.get_type());
    while (!complete) {
        
        complete = request.write(stream);
        request_deserialized->read(stream);
    }
    for (auto i = 0; i < request.patches.size(); ++i) {
        assert(upload_request->patches[i]->name == request.patches[i]->name);
        assert(upload_request->patches[i]->file_size == request.patches[i]->file_size);
        assert(upload_request->patches[i]->platform == request.patches[i]->platform);
        assert(upload_request->patches[i]->platform == request.patches[i]->platform);
        const auto equal = std::memcmp(upload_request->patches[i]->data,
            request.patches[i]->data, upload_request->patches[i]->file_size);
        assert(equal == 0);
    }
    for (auto& patch : request.patches) {
        patch->data = nullptr;
    }
    delete[] test_buffer;
}

void serialize_upload_response() {
    ph::upload_patch_response response;
    for (auto i = 0; i < 5; ++i) {
        const auto name = "random_name" + std::to_string(i);
        const auto size = i * 100000;
        for (auto i = 0; i < 5; ++i) {
            auto testp = std::make_shared<ph::patch>();
            testp->platform = "WindowsClient";
            testp->revision = 1;
            testp->name = "random_name" + std::to_string(i);
            testp->file_size = std::rand() % 100000;
        }
    }
    hope::io::event_loop::fixed_size_buffer b;
    ph::event_loop_stream_wrapper stream(b);
    
    response.write(stream);

    auto response_deserialized = ph::message::peek_response(stream);
    response_deserialized->read(stream);
   
    assert(response_deserialized->get_type() == response.get_type());
    auto upload_response = static_cast<ph::upload_patch_response *>(response_deserialized);
    for (auto i = 0; i < response.patches.size(); ++i) {
        assert(response.patches[i]->file_size == upload_response->patches[i]->file_size);
        assert(response.patches[i]->name == upload_response->patches[i]->name);
        assert(response.patches[i]->revision == upload_response->patches[i]->revision);
        assert(response.patches[i]->platform == upload_response->patches[i]->platform);
    }
}

void serialize_get_request() {
    ph::get_patches_request request;
    request.platform = "WindowsClient";
    request.revision = 1;
    hope::io::event_loop::fixed_size_buffer b;
    ph::event_loop_stream_wrapper stream(b);
    
    request.write(stream);

    auto request_deserialized = ph::message::peek_request(stream);
    request_deserialized->read(stream);
   
    assert(request_deserialized->get_type() == request.get_type());
    const auto get_request = static_cast<ph::get_patches_request *>(request_deserialized);
    assert(get_request->platform == request.platform);
    assert(get_request->revision == request.revision);
}

void serialize_get_response() {
    // same with upload request
}

void run_tests() {
    serialize_list_request();
    serialize_list_response();
    serialize_delete_request();
    serialize_delete_response();
    serialize_upload_request();
    serialize_upload_response();
    serialize_get_request();
    serialize_get_response();
}