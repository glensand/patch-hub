cmake_minimum_required(VERSION 3.22)

project(crash-reporter)

add_subdirectory(service)
add_subdirectory(client)
add_subdirectory(test)

add_subdirectory(third-party/hope-logger/lib)
add_subdirectory(third-party/hope-threading/lib)
add_subdirectory(third-party/hope-io/lib)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
