/* Copyright (C) 2025 Gleb Bezborodov - All Rights Reserved
* You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/patch-hub
 */

#pragma once

#include <string>
#include "hope_logger/log_helper.h"

#undef ERROR
#define INFO hope::log::log_level::info
#define ERROR hope::log::log_level::error
#define LOG(Prior) HOPE_INTERIOR_LOG(Prior, *glob_logger)
#define CLOG(Prior, Cnd) if ((Cnd)) HOPE_INTERIOR_LOG(Prior, *glob_logger)

extern hope::log::logger* glob_logger;

namespace ph {

    using revision_t = uint32_t;

    struct patch final {
        std::string name;
        std::size_t file_size;
        uint8_t* data{ nullptr };
        ~patch() {
            delete[] data;
        }
    };

    class service {
    public:
        virtual ~service() = default;
    };

    service* create_service();

}