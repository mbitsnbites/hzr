# -*- mode: CMake; tab-width: 2; indent-tabs-mode: nil; -*-
cmake_minimum_required(VERSION 3.5)

# We require C++11 for building this library.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable warnings, and treat them as errors.
if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -W -Wall -Wextra -pedantic -Werror")
  set(CMAKE_CPP_FLAGS "${CMAKE_CPP_FLAGS} -W -Wall -Wextra -pedantic -Werror")
elseif("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Weverything -Wno-padded -Werror")
  set(CMAKE_CPP_FLAGS "${CMAKE_CPP_FLAGS} -Weverything -Wno-padded -Werror")
elseif("${CMAKE_C_COMPILER_ID}" STREQUAL "MSVC")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /WX")
  set(CMAKE_CPP_FLAGS "${CMAKE_C_FLAGS} /WX")
endif()

# Enable SSE4.2 (for the faster CRC32C routine).
# TODO(m): Make a proper test for this (target is x86 and GNU/?). Also, possibly
# only enable for a specific source file.
if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse4.2")
endif()

set(lib_includes
    include/libhzr.h)

set(lib_sources
    lib/hzr_crc32.c
    lib/hzr_decode.cpp
    lib/hzr_encode.cpp)

add_library(hzr ${lib_sources} ${lib_includes})

target_include_directories(hzr PUBLIC include)

install(TARGETS hzr
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)
install(FILES ${lib_includes} DESTINATION include)

