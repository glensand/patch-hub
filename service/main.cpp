#include "service.h"

#include "hope_logger/logger.h"
#include "hope_logger/ostream.h"

#include <iostream>

hope::log::logger* glob_logger;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Provide slack token";
        return -1;
    }

    glob_logger = new hope::log::logger(
        *hope::log::create_multy_stream({
            hope::log::create_file_stream("crash-reporter.txt"),
            hope::log::create_console_stream()
        })
    );

    auto serv = ph::create_service();

    return 0;
}