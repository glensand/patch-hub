#include "ph/service.h"

#include "hope_logger/logger.h"
#include "hope_logger/ostream.h"

hope::log::logger* glob_logger;

int main() {
    glob_logger = new hope::log::logger(
        *hope::log::create_multy_stream({
            hope::log::create_file_stream("phub.txt"),
            hope::log::create_console_stream()
        })
    );

    auto serv = ph::create_service();

    return 0;
}