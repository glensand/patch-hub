#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#include "ph/client.h"
#include "consolelib/disco.h"
#include "hope-io/net/init.h"

char* load_file(const std::string& filename, uint32_t& size);
void write_to_file(const std::string& filename, void* data, uint32_t size);

int main(int argc, char *argv[]) {
    std::string ip = "127.0.0.1";
    if (argc > 1) {
        ip = argv[1];
        std::cout << "Parsed ip" << ip << '\n';
    } else {
        std::cout << "Host is not provided, localhost will be used\n";
    }
    int port = 1556;
    if (argc > 2) {
        port = std::stoi(argv[4]);
    }

    hope::io::init();

    auto client = ph::client::create(ip, port);
    bool exit = false;

    disco::completer_impl completer; // im not sure why i added this 4 years ago
    disco::string_command_executor invoker(new disco::function_proxy_impl, new disco::variable_proxy_impl,
            [&completer](auto&& name) { completer.add_name(name); });

    invoker.create_function("list", [client] {
        std::cout << "List patches...\n";
        const auto patches = client->list();
        for (const auto& p : patches) {
            p->print();
        }
    });
    invoker.create_function("upload_from_dir", [client](const std::string& platform,
        std::size_t revision, const std::string& dir) {
        std::cout << "Upload from dir[" << dir << "]...\n";
        ph::client::plist_t plist;
        try {
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (entry.is_regular_file()) {
                    auto p = std::make_shared<ph::patch>();
                    p->name = "PH_Redist_" + entry.path().filename().string();
                    p->tag = platform + "_" + std::to_string(revision);
                    p->data = (uint8_t*)load_file(entry.path().string(), p->file_size);
                    if (p->data == nullptr) {
                        std::cout << "Cannot load file[" << entry.path().string() << "]\n";
                    } else {
                        plist.push_back(std::move(p));
                        std::cout << entry.path().string() << "\n";
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
        }
        const auto uploaded = client->upload(plist);
        std::cout << "Uploaded patches:\n";
        for (const auto& p : uploaded) {
            p->print();
        }
        for (const auto& p : plist) {
            delete p->data;
        }
    });
    invoker.create_function("upload_file", [client](const std::string& platform,
        std::size_t revision, const std::string& filepath) {
        std::cout << "Upload patch file[" << filepath << "]...\n";
        std::filesystem::path p(filepath);
        auto patch = std::make_shared<ph::patch>();
        patch->name = p.filename().string();
        patch->tag = platform + "_" + std::to_string(revision);
        patch->data = (uint8_t*)load_file(p.string(), patch->file_size);
        if (patch->data == nullptr) {
            std::cout << "Cannot load file\n";
        } else {
            const auto uplaoded = client->upload({patch} );
            std::cout << "Uploaded patches:\n";
            for (const auto& p : uplaoded) {
                p->print();
            }
        }
        delete patch->data;
    });
    invoker.create_function("download", [client](const std::string& platform, std::size_t revision, const std::string& outdir) {
        std::cout << "Download patch files[" << platform << "]" "[" << revision <<"]" << " to[" << outdir << "]...\n";
        const auto tag = platform + "_" + std::to_string(revision);
        const auto downloaded = client->download(tag);
        for (const auto& p : downloaded) {
            p->print();
            const std::string path = outdir + "/" + p->name;
            write_to_file(path, p->data, p->file_size);
        }
    });
    invoker.create_function("delete", [client](const std::string& platform, std::size_t revision) {
        std::cout << "Delete patch...\n";
        const auto tag = platform + "_" + std::to_string(revision);
        const auto& removed = client->pdelete(tag);
        for (const auto& p : removed) {
            p->print();
        }
    });
    invoker.create_function("exit", [&exit] {
        exit = true;
    });
    invoker.create_function("help", [&exit] {
        std::cout << "// ------------------- API -------------------//\n";
        std::cout << "[list]" <<
            "-list all available patches (will be requested from server)\n";
        std::cout << R"([delete("PlatformName", Revision)])" <<
            "-delete all patches for specified revision and platform\n";
        std::cout << R"([upload_file("PlatformName", Revision, "FullPath")])" <<
            "-uploads patch for specified revision and platform\n";
        std::cout << R"([upload_from_dir("PlatformName", Revision, "DirPath")])" <<
            "-uploads patches for specified revision and platform\n";
        std::cout << R"([download("PlatformName", Revision, "OutPath")])" <<
            "-downloads patches for specified revision and platform, stores to out dir\n";
        std::cout << "// ------------------- Examples -------------------//\n";
        std::cout << "delete(\"WindowsClient\", 321800)\n";
        std::cout << "upload_file(\"WindowsClient\", 321800, \"c:/patches/your_app/paks/win0.pak\")\n";
        std::cout << "upload_from_dir(\"WindowsClient\", 321800, \"c:/patches/your_app/paks\")\n";
        std::cout << "download(\"WindowsClient\", 321800, \"c:/game/content/paks/mods\")\n";
    });
    while (!exit) {
        std::string query;
        std::getline(std::cin, query);

        try {
            std::cout << invoker.invoke(query) << '\n';
        }
        catch(const disco::exception& ex) {
            std::cout << "An exception occurred: " << ex.what() << '\n';
        }
        catch(const std::exception& ex) {
            std::cout << "An exception occurred: " << ex.what() << '\n';
        }
    }
    delete client;

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

void write_to_file(const std::string& filename, void* data, uint32_t size) {
    std::ofstream file(filename, std::ios::binary);
    file.write((char*)(data), size);
    file.close();
}
