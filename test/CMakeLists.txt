# -*- mode: CMake; tab-width: 2; indent-tabs-mode: nil; -*-
cmake_minimum_required(VERSION 3.0)

if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -W -Wall -Wextra -pedantic -std=c99")
elseif("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Weverything -Wno-padded -Wno-cast-align -std=c99")
endif()

find_package(ZLIB REQUIRED)
if(ZLIB_FOUND)
  include_directories(${ZLIB_INCLUDE_DIRS})
  add_definitions(-DHZR_HAS_ZLIB)
endif()

set(demo_sources
    demo.c)

set(benchmark_sources
    benchmark.c)

add_executable(demo ${demo_sources})
target_link_libraries(demo hzr)

add_executable(benchmark ${benchmark_sources})
target_link_libraries(benchmark hzr)
if(ZLIB_FOUND)
  target_link_libraries(benchmark ${ZLIB_LIBRARIES})
endif()

