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

cmake_minimum_required(VERSION 3.10)

project(hzr C)

# Build options.
option(HZR_ENABLE_TESTS      "Enable unit tests"                on)
option(HZR_ENABLE_SANITIZERS "Enable sanitizer instrumentation" off)

# Enable sanitizers.
if(HZR_ENABLE_SANITIZERS)
  if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU" OR
     "${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
    message("HZR: Using sanitizers")
    set(SANITIZERS "-fsanitize=address -fsanitize=undefined -fsanitize=leak")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${SANITIZERS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SANITIZERS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${SANITIZERS}")
  endif()
endif()

# Library.
add_subdirectory(src)

# Unit tests.
if(HZR_ENABLE_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
