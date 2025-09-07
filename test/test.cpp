#include "hope_logger/logger.h"
#include "hope_logger/ostream.h"

void run_tests();
void run_integration();

hope::log::logger* glob_logger;

int main() {
    glob_logger = new hope::log::logger(
    *hope::log::create_multy_stream({
            hope::log::create_file_stream("phub.txt"),
            hope::log::create_console_stream()
        })
    );

    run_tests();
    run_integration();
}
