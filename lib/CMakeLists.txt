cmake_minimum_required(VERSION 3.11)

project(phlib)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

file(GLOB PH_HEADERS
    *.h
    ph/*.h
)

file(GLOB PH_SOURCES
    *.cpp
    ph/*.cpp
)

add_library(${PROJECT_NAME} STATIC ${PH_HEADERS} ${PH_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(${PROJECT_NAME} PRIVATE "BUILD_DEBUG=$<IF:$<CONFIG:Debug>,1,0>")
target_compile_definitions(${PROJECT_NAME} PRIVATE "-DCMAKE_EXPORT_COMPILE_COMMANDS=1")

target_include_directories(${PROJECT_NAME} PUBLIC ../third-party/hope-logger/lib)
target_include_directories(${PROJECT_NAME} PUBLIC ../third-party/hope-threading/lib)
target_include_directories(${PROJECT_NAME} PUBLIC ../third-party/hope-io/lib)
target_include_directories(${PROJECT_NAME} PUBLIC ../third-party/congenial-disco/lib)
