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
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Weverything -Wno-padded -Wno-cast-align -Werror")
  set(CMAKE_CPP_FLAGS "${CMAKE_CPP_FLAGS} -Weverything -Wno-padded -Werror")
elseif("${CMAKE_C_COMPILER_ID}" STREQUAL "MSVC")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /WX")
  set(CMAKE_CPP_FLAGS "${CMAKE_C_FLAGS} /WX")
endif()

set(lib_includes
    include/libhzr.h)

set(lib_sources
    lib/hzr_crc32c.c
    lib/hzr_decode.c
    lib/hzr_encode.cpp)

# Enable fast SSE 4.2-optimized CRC32C routine.
if("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "86")
  message("Using x86 optimizations.")
  add_definitions("-DHZR_ARCH_X86")
  set(lib_sources ${lib_sources} lib/hzr_crc32c_sse4.c)
  if(("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU") OR ("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang"))
    set_source_files_properties(lib/hzr_crc32c_sse4.c PROPERTIES COMPILE_FLAGS "-msse4.2")
  endif()
endif()

add_library(hzr ${lib_sources} ${lib_includes})

target_include_directories(hzr PUBLIC include)

install(TARGETS hzr
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)
install(FILES ${lib_includes} DESTINATION include)

