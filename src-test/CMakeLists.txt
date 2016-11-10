# -*- mode: CMake; tab-width: 2; indent-tabs-mode: nil; -*-
#-------------------------------------------------------------------------------
#  hzr - A Huffman + RLE compression library.
#
# Copyright (C) 2016 Marcus Geelnard
#
# This software is provided 'as-is', without any express or implied warranty.
# In no event will the authors be held liable for any damages arising from the
# use of this software.
#
# Permission is granted to anyone to use this software for any purpose,
# including commercial applications, and to alter it and redistribute it
# freely, subject to the following restrictions:
#
#  1. The origin of this software must not be misrepresented; you must not
#     claim that you wrote the original software. If you use this software in
#     a product, an acknowledgment in the product documentation would be
#     appreciated but is not required.
#  2. Altered source versions must be plainly marked as such, and must not be
#     misrepresented as being the original software.
#  3. This notice may not be removed or altered from any source distribution.
#-------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.0)

add_subdirectory(third-party/catch)

if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -W -std=c99")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -W -std=c++03")
endif()

list(APPEND HZR_TEST_LIBRARIES catch)
list(APPEND HZR_TEST_LIBRARIES hzr)

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

set(benchmark_sources
    benchmark.c
    random.c)

add_executable(compression_tests compression_tests.cpp)
target_link_libraries(compression_tests ${HZR_TEST_LIBRARIES})

add_executable(benchmark ${benchmark_sources})
target_link_libraries(benchmark ${HZR_TEST_LIBRARIES})


