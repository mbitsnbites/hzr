# -*- mode: CMake; tab-width: 2; indent-tabs-mode: nil; -*-
cmake_minimum_required(VERSION 3.5)

set(demo_sources
    demo.c)

set(benchmark_sources
    benchmark.c)

add_executable(demo ${demo_sources})
target_link_libraries(demo hzr)

add_executable(benchmark ${benchmark_sources})
target_link_libraries(benchmark hzr)

