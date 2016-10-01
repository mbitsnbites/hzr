# -*- mode: CMake; tab-width: 2; indent-tabs-mode: nil; -*-
cmake_minimum_required(VERSION 3.5)

set(demo_sources
    demo.c)

add_executable(demo ${demo_sources})
target_link_libraries(demo hzr)

