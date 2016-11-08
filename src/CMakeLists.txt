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

# Enable warnings, and treat them as errors.
if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -W -Wall -Wextra -pedantic -Werror -std=c99")
elseif("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Weverything -Wno-padded -Wno-cast-align -Werror -std=c99")
elseif("${CMAKE_C_COMPILER_ID}" STREQUAL "MSVC")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /WX")
endif()

set(lib_includes
    include/libhzr.h)

set(lib_sources
    lib/hzr_crc32c.c
    lib/hzr_decode.c
    lib/hzr_encode.c)

# Enable fast SSE 4.2-optimized CRC32C routine.
if("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "86")
  message("Using x86 optimizations.")
  add_definitions("-DHZR_ARCH_X86")
  set(lib_sources ${lib_sources} lib/hzr_crc32c_sse4.c)
  if(("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU") OR ("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang"))
    set_source_files_properties(lib/hzr_crc32c_sse4.c PROPERTIES COMPILE_FLAGS "-msse4.2")
  endif()
endif()

# Enable fast ARMv8-optimized CRC32C routine.
if("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "arm")
  message("Using ARM optimizations.")
  add_definitions("-DHZR_ARCH_ARM")
  set(lib_sources ${lib_sources} lib/hzr_crc32c_armv8.c)
endif()


add_library(hzr ${lib_sources} ${lib_includes})

target_include_directories(hzr PUBLIC include)

install(TARGETS hzr
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)
install(FILES ${lib_includes} DESTINATION include)

