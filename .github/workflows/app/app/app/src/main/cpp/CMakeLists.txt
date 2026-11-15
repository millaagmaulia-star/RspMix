cmake_minimum_required(VERSION 3.22.1)
project("vocalfx")

find_package(oboe REQUIRED CONFIG)

add_library(vocalfx SHARED AudioEngine.cpp)

target_link_libraries(vocalfx
        log
        oboe::oboe)
