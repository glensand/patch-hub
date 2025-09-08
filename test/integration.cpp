#include "ph/service.h"
#include "ph/client.h"
#include "ph/message.h"
#include <thread>
#include <unordered_set>
#include <cstring>
s
// uploaded patches
ph::client::plist_t list;

void run_upload() {
    std::cout << "// ----------- Upload patches // -----------\n";
    auto client = ph::client::create("localhost", 1555);
    const auto uploaded = client->upload(list);
    for (auto p : uploaded) {
        p->print();
    }
    delete client;
}

void run_list() {
    std::cout << "// ----------- List patches // -----------\n";
    auto client = ph::client::create("localhost", 1555);
    const auto plist = client->list();
    for (const auto& p : plist) {
        p->print();
        bool found = false;
        for (const auto& gp : list) {
            if (p->name == gp->name && p->revision == gp->revision && p->platform == gp->platform) {
                found = true;
                assert(p->file_size == gp->file_size);
            }
        }
        assert(found);
    }
}

void run_download() {
    std::cout << "// ----------- Download patches // -----------" << std::endl;
    auto client = ph::client::create("localhost", 1555);
    const auto plist = client->list();
    struct patch_key final {
        ph::revision_t revision{};
        std::string platform;
        bool operator==(const patch_key& other) const {
            return revision == other.revision && platform == other.platform;
        }
        struct hash final {
            std::size_t operator()(const patch_key& k) const {
                std::size_t h1 = std::hash<ph::revision_t>{}(k.revision);
                std::size_t h2 = std::hash<std::string>{}(k.platform);
                return h1 ^ (h2 << 1);
            }
        };
    };
    std::unordered_set<patch_key, patch_key::hash> patches;
    for (const auto& p : plist) {
        patch_key k{ p->revision, p->platform };
        patches.emplace(k);
    }
    for (const auto& [rev, platform] : patches) {
        const auto downloaded = client->download(rev, platform);
        for (const auto& p : downloaded) {
            p->print();
            bool found = false;
            for (const auto& gp : list) {
                if (p->name == gp->name && p->revision == gp->revision && p->platform == gp->platform) {
                    found = true;
                    assert(p->file_size == gp->file_size);
                    const auto eq = std::memcmp(p->data, gp->data, p->file_size);
                    assert(eq == 0);
                }
            }
            assert(found);
        }
    }
}

void run_delete() {
    std::cout << "// ----------- Delete patches // -----------" << std::endl;
    auto client = ph::client::create("localhost", 1555);
    std::vector<std::shared_ptr<ph::patch>> removedall;
    for (const auto& p : list) {
        const auto removed = client->pdelete(p->revision, p->platform);
        assert(!removed.empty());
        for (const auto& rp : removed) {
            removedall.emplace_back(rp);
        }
    }
    for (const auto& r : removedall) {
        for (auto it = begin(list); it != end(list); ) {
            auto& candidate = *it;
            if (candidate->name == r->name) {
                candidate->data = nullptr;
                it = list.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void run_integration() {
    ph::service* sv = nullptr;
    std::thread servicet([&]{
        sv = ph::create_service();
        sv->run();
    });

    // prepare mock patches
    constexpr static auto buffer_size = 64 * 1024;
    auto* test_buffer = new uint8_t[buffer_size];
    for (auto i = 0; i < 5; ++i) {
        test_buffer[i] = std::rand() % 256;
        auto p = std::make_shared<ph::patch>();
        p->name = "random_name" + std::to_string(i);
        p->file_size = 31 * 1024 + std::rand() % (32 * 1024);
        p->data = test_buffer;
        p->platform = "random_platform" + std::to_string(i);
        p->revision = i * 1000 + 1;
        list.emplace_back(std::move(p));
    }

    while (!sv) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    run_upload();
    run_list();
    run_download();
    run_delete();

    for (auto& p : list) {
        p->data = nullptr;
    }
    delete [] test_buffer;

    sv->stop();
    servicet.join();
    delete sv;
}
