#include "ph/service.h"
#include "ph/client.h"
#include "ph/message.h"
#include <thread>
#include <unordered_set>
#include <cstring>

// uploaded patches
ph::client::plist_t list;

void run_upload(int port = 1555) {
    std::cout << "// ----------- Upload patches // -----------\n";
    auto client = ph::client::create("localhost", port);
    const auto uploaded = client->upload(list);
    for (auto p : uploaded) {
        p->print();
    }
    delete client;
}

void run_list(int port = 1555) {
    std::cout << "// ----------- List patches // -----------\n";
    auto client = ph::client::create("localhost", port);
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

void run_download(int port = 1555) {
    std::cout << "// ----------- Download patches // -----------" << std::endl;
    auto client = ph::client::create("localhost", port);
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

void run_delete(int port = 1555) {
    std::cout << "// ----------- Delete patches // -----------" << std::endl;
    auto client = ph::client::create("localhost", port);
    std::vector<std::shared_ptr<ph::patch>> removedall;
    for (const auto& p : list) {
        const auto removed = client->pdelete(p->revision, p->platform);
        assert(!removed.empty());
        for (const auto& rp : removed) {
            removedall.emplace_back(rp);
        }
    }
}

void run_cache() {
    std::cout << "// ----------- Run cache test // -----------" << std::endl;
    // I think we need tsome time here
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    ph::service* sv = nullptr;
    std::thread servicet([&] {
        sv = ph::create_service();
        std::atomic_thread_fence(std::memory_order_seq_cst);
        sv->run(1556);
    });
    while (!sv) { std::this_thread::yield(); }
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // even more time to start listen
    run_upload(1556);
    sv->stop();
    servicet.join();
    delete sv;

    sv = nullptr;
    servicet = std::thread([&] {
        sv = ph::create_service();
        sv->run(1557);
    });
    while (!sv) { std::this_thread::yield(); }
    run_list(1557);
    run_download(1557);
    sv->stop();
    servicet.join();
    delete sv;
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

    sv->stop();
    servicet.join();
    delete sv;

    run_cache();

    for (auto& p : list) {
        p->data = nullptr;
    }
    delete [] test_buffer;
}
