#include <csignal>
#include <functional>

#include "ph/service.h"

#include "hope_logger/logger.h"
#include "hope_logger/ostream.h"

hope::log::logger* glob_logger;
std::function<void()> glob_handler;
static void signal_handler(int signal) {
    if (signal == SIGINT) {
        glob_handler();
    }
}
int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    glob_logger = new hope::log::logger(
        *hope::log::create_multy_stream({
            hope::log::create_file_stream("logs/phub.txt"),
            hope::log::create_console_stream()
        })
    );
    int port = 1556;
    if (argc > 1) {
	    //port = std::stoi(argv[1]);
    }

    auto serv = ph::create_service();
    glob_handler = [serv] {
        serv->stop();
    };
    serv->run(port);

    return 0;
}