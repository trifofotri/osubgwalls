cmake_minimum_required(VERSION 3.16)
project(osubgwalls C)

find_package(CURL REQUIRED)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(osubgwalls
    third_party/cjson/cJSON.c
    src/osubgwalls.c
)

target_include_directories(osubgwalls PRIVATE include third_party)
target_link_libraries(osubgwalls PRIVATE CURL::libcurl zip)