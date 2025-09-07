/* Copyright (C) 2025 Gleb Bezborodov - All Rights Reserved
* You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/patch-hub
 */

#pragma once

#include <format>
#include <iostream>
#include <vector>

namespace ph {

    class client {
    public:
        struct patch final {
            std::string name;
            std::string platform;
            std::size_t file_size{};
            std::size_t revision{};
            uint8_t* data{};

            void print() const {
                std::cout  << "Patch:\n"
                    << "  Name: " << name << '\n'
                    << "  Platform: " << platform << '\n'
                    << "  File size: " << file_size << " bytes\n"
                    << "  Revision: " << revision << '\n';
            }
        };

        virtual ~client() = default;

        using plist_t = std::vector<patch>;
        // list all available patches, data will be empty
        virtual plist_t list() = 0;
        // downloads all available patches for revision and platform
        virtual plist_t download(std::size_t revision, const std::string& platform) = 0;
        // store or replace specified patches, returns list with uploaded patches
        virtual plist_t upload(const plist_t& plist) = 0;
        // tries to remove specified patches, returns list of removed patches
        virtual plist_t pdelete(std::size_t revision, const std::string& platform) = 0;

        static client* create(const std::string& ip, int port);
    };
}