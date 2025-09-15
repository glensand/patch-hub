#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#include "ph/client.h"
#include "consolelib/disco.h"
#include "hope-io/net/init.h"
#include "ph/service.h"

char* load_file(const std::string& filename, uint32_t& size);

int main(int argc, char *argv[]) {
    if (argc < 4) {
        std::cout << "Usage:ip tag_prefix path\n";
        return -1;
    }
    std::string ip = argv[1];
    std::string tag = argv[2];
    std::string path = argv[3];
    int port = 1556;
    if (argc > 5) {
        port = std::stoi(argv[4]);
    }

    hope::io::init();

    auto client = ph::client::create(ip, 1556);
    ph::client::plist_t plist;
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        if (entry.is_regular_file()) {
            auto p = std::make_shared<ph::patch>();
            p->name = "PH_Redist_" + entry.path().filename().string();
            p->tag = tag;
            p->data = (uint8_t*)load_file(entry.path().string(), p->file_size);
            if (p->data == nullptr) {
                std::cout << "Cannot load file[" << entry.path().string() << "]\n";
            }
            else {
                plist.push_back(std::move(p));
                std::cout << entry.path().string() << "\n";
            }
        }
    }

    const auto uploaded = client->upload(plist);
    std::cout << "Uploaded patches:\n";
    for (const auto& p : uploaded) {
        p->print();
    }

    return 0;
}

char* load_file(const std::string& filename, uint32_t& size) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) return nullptr;

    size = file.tellg();
    file.seekg(0, std::ios::beg);

    char* buffer = new char[size];
    if (!file.read(buffer, size)) {
        delete[] buffer;
        return nullptr;
    }
    return buffer;
}
