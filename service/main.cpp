#include "service.h"

#include "hope_logger/log_helper.h"
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

    raper::service::config cfg;
    cfg.port = 11338;
    cfg.slack_channel = "C091T3X2NNR";
    cfg.slack_token = argv[1];
    auto serv = raper::create_service(cfg);

    return 0;
}