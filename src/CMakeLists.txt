# -*- mode: CMake; tab-width: 2; indent-tabs-mode: nil; -*-
cmake_minimum_required(VERSION 3.5)

# We require C++11 for building this library.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(lib_includes
    include/libhzr.h)

set(lib_sources
    lib/hzr_decode.cpp
    lib/hzr_encode.cpp)

add_library(hzr ${lib_sources} ${lib_includes})

target_include_directories(hzr PUBLIC include)

install(TARGETS hzr
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)
install(FILES ${lib_includes} DESTINATION include)

