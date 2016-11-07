# -*- mode: CMake; tab-width: 2; indent-tabs-mode: nil; -*-
cmake_minimum_required(VERSION 3.0)

if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -W -Wall -Wextra -pedantic -std=c99")
elseif("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Weverything -Wno-padded -Wno-cast-align -std=c99")
endif()

list(APPEND HZR_TEST_LIBRARIES "hzr")

if (UNIX AND NOT APPLE)
  find_library(MATH_LIBRARY m)
  if(MATH_LIBRARY)
    list(APPEND HZR_TEST_LIBRARIES ${MATH_LIBRARY})
  endif()
endif()

find_package(ZLIB)
if(ZLIB_FOUND)
  include_directories(${ZLIB_INCLUDE_DIRS})
  add_definitions(-DHZR_HAS_ZLIB)
  list(APPEND HZR_TEST_LIBRARIES ${ZLIB_LIBRARIES})
endif()

set(demo_sources
    demo.c)

set(benchmark_sources
    benchmark.c
    random.c)

add_executable(demo ${demo_sources})
target_link_libraries(demo ${HZR_TEST_LIBRARIES})

add_executable(benchmark ${benchmark_sources})
target_link_libraries(benchmark ${HZR_TEST_LIBRARIES})

