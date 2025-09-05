#include <thread>
#include <iostream>
#include <unordered_map>
#include <array>
#include <vector>
#include <fstream>
#include <memory>
#include <filesystem>

#include "hope-io/net/event_loop.h"
#include "hope-io/net/stream.h"
#include "hope-io/net/factory.h"

#include "hope_logger/log_helper.h"
#include "hope_logger/logger.h"
#include "hope_logger/ostream.h"

#include "../service/stream_wrapper.h"
#include "../service/message.h"

const std::string fishText[] = {
    "One fish, two fish, red fish, blue fish.",
    "Black fish, blue fish, old fish, new fish.",
    "This one has a little star.",
    "This one has a little car.",
    "Say! What a lot of fish there are.",
    "Some are red, and some are blue.",
    "Some are old and some are new.",
    "Some are sad, and some are glad,",
    "And some are very, very bad.",
    "Why are they sad and glad and bad?",
    "I do not know, go ask your dad."
};

constexpr static auto ChunkSize = hope::io::event_loop::fixed_size_buffer::buffer_size - 5;
// * You should have received a copy of the MIT license with

int main(int argc, char *argv[]) {
    srand(time(nullptr));
    auto random_number = std::rand();
    const std::string filename = std::to_string(random_number) + "big_file.txt";
    const int targetLines = 100000;
    
    std::ofstream file(filename);
    if (!file) {
        std::cerr << "Error opening file for writing!" << std::endl;
        return 1;
    }

    std::cout << "Generating file with " << targetLines << " lines..." << std::endl;

    int fishSize = sizeof(fishText) / sizeof(fishText[0]);
    for (int i = 0; i < targetLines; ++i) {
        file << i + 1 << ": " << fishText[i % fishSize] << "\n";
    }

    std::cout << "Generated" << std::endl;

    auto stream = hope::io::create_stream();
    const auto file_size = std::filesystem::file_size(filename);

    message m;
    m.client_name = "My client";
    m.pc_name = "My PC";
    m.file_name = "report_" + filename;
    m.file_size = file_size;
    m.total_chunks = file_size / ChunkSize;
    if (m.total_chunks * ChunkSize < file_size) {
        ++m.total_chunks;
    }

    hope::io::event_loop::fixed_size_buffer buffer;
    event_loop_stream_wrapper wrapper(buffer);
    wrapper.begin_write();
    m.write(wrapper);
    wrapper.end_write();

    stream->connect("localhost", std::stoi(argv[1]));
    auto message_serialized = buffer.used_chunk();
    stream->write(message_serialized.first, message_serialized.second);
    
    (void)stream->read<uint32_t>();
    (void)stream->read<bool>();

    std::ifstream read_file(filename, std::ios::binary);
    std::size_t total_bytes_read = 0;
    std::size_t chunk = 0;
    while (total_bytes_read < file_size) {
        char buffer[ChunkSize];
        size_t bytes_read = read_file.read(buffer, ChunkSize).gcount();
        if (!bytes_read) {
            std::cout << "Cannot read from file, close" << std::endl;
            return -1;
        }
        stream->write(uint32_t(bytes_read) + 4); // size of payload +  size of size :)
        stream->write(buffer, bytes_read);
        total_bytes_read += bytes_read;

        std::cout << "Sendt chunk:" << chunk << std::endl;

        // part of our protocol
        (void)stream->read<uint32_t>();
        (void)stream->read<bool>();
    }

    return 0;
}