cmake_minimum_required(VERSION 3.22)
project(service)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB SERVICE_HEADERS
    *.h
)

file(GLOB SERVICE_SOURSES
    *.cpp
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
add_executable(${PROJECT_NAME} ${SERVICE_HEADERS} ${SERVICE_SOURSES})
target_compile_definitions(${PROJECT_NAME} PRIVATE "BUILD_DEBUG=$<IF:$<CONFIG:Debug>,1,0>")
target_compile_definitions(${PROJECT_NAME} PRIVATE "-DCMAKE_EXPORT_COMPILE_COMMANDS=1")

target_include_directories(${PROJECT_NAME} PUBLIC ../third-party/hope-logger/lib)
target_include_directories(${PROJECT_NAME} PUBLIC ../third-party/hope-threading/lib)
target_include_directories(${PROJECT_NAME} PUBLIC ../third-party/hope-io/lib)

target_link_libraries(${PROJECT_NAME} hope_logger)
target_link_libraries(${PROJECT_NAME} hope_thread)
target_link_libraries(${PROJECT_NAME} hope-io)
