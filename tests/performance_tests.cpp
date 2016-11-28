//------------------------------------------------------------------------------
//  hzr - A Huffman + RLE compression library.
//
// Copyright (C) 2016 Marcus Geelnard
//
// This software is provided 'as-is', without any express or implied warranty.
// In no event will the authors be held liable for any damages arising from the
// use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
//  1. The origin of this software must not be misrepresented; you must not
//     claim that you wrote the original software. If you use this software in
//     a product, an acknowledgment in the product documentation would be
//     appreciated but is not required.
//  2. Altered source versions must be plainly marked as such, and must not be
//     misrepresented as being the original software.
//  3. This notice may not be removed or altered from any source distribution.
//------------------------------------------------------------------------------

#include <catch.hpp>

#include <cstring>
#include <iostream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/time.h>
#endif

#include <libhzr.h>

#ifdef HZR_HAS_ZLIB
#include <zlib.h>
#endif  // HZR_HAS_ZLIB

#include "random.h"

namespace {

const size_t MAX_UNCOMPRESSED_SIZE = 131072;

const size_t SIZES[] = {MAX_UNCOMPRESSED_SIZE,
                        MAX_UNCOMPRESSED_SIZE / 4,
                        MAX_UNCOMPRESSED_SIZE / 8,
                        MAX_UNCOMPRESSED_SIZE / 32};
const size_t NUM_SIZES = sizeof(SIZES) / sizeof(SIZES[0]);

// This is an approximation (rounded up) of the maximum compressed size.
const size_t MAX_COMPRESSED_SIZE =
    MAX_UNCOMPRESSED_SIZE + (MAX_UNCOMPRESSED_SIZE >> 13) + 4;

// Statically allocate memory for the compression/decompression.
unsigned char s_uncompressed[MAX_UNCOMPRESSED_SIZE];
unsigned char s_compressed[MAX_COMPRESSED_SIZE];
unsigned char s_uncompressed2[MAX_UNCOMPRESSED_SIZE];

double get_time() {
#if defined(_WIN32)
  static double s_period = 0.0;
  if (s_period == 0.0) {
    LARGE_INTEGER freq = 0;
    QueryPerformanceFrequency(&freq);
    s_period = 1.0 / (double)freq;
  }
  LARGE_INTEGER count;
  QueryPerformanceCounter(&count);
  return s_period * (double)count;
#else
  struct timeval t;
  gettimeofday(&t, NULL);
  return ((double)t.tv_sec) + 0.000001 * (double)t.tv_usec;
#endif
}

const int NUM_BENCHMARK_ITERATIONS = 1000;

void print_results(const char* str, double dt, size_t num_bytes) {
  double speed = static_cast<double>(NUM_BENCHMARK_ITERATIONS * num_bytes) / dt;
  std::cout << "  " << str << ": " << speed / (1024.0 * 1024.0) << " MB/s\n";
}

void perform_test(size_t uncompressed_size) {
  double t0, dt;
  int success_count;

  std::cout << " Size: " << uncompressed_size << "\n";

  // Check that the compressed buffer is large enough according to the HZR API.
  const size_t max_compressed_size = hzr_max_compressed_size(uncompressed_size);
  REQUIRE(sizeof(s_compressed) >= max_compressed_size);

  // Compress the data.
  success_count = 0;
  size_t compressed_size = 0;
  t0 = get_time();
  for (int i = 0; i < NUM_BENCHMARK_ITERATIONS; ++i) {
    hzr_status_t status =
        hzr_encode(s_uncompressed, uncompressed_size, s_compressed,
                   max_compressed_size, &compressed_size);
    if (status == HZR_OK) {
      ++success_count;
    }
  }
  dt = get_time() - t0;
  print_results("Encode", dt, uncompressed_size);
  CHECK(success_count == NUM_BENCHMARK_ITERATIONS);

  // Verify the compressed data.
  success_count = 0;
  size_t uncompressed_size2 = 0;
  t0 = get_time();
  for (int i = 0; i < NUM_BENCHMARK_ITERATIONS; ++i) {
    hzr_status_t status =
        hzr_verify(s_compressed, compressed_size, &uncompressed_size2);
    if (status == HZR_OK) {
      ++success_count;
    }
  }
  dt = get_time() - t0;
  print_results("Verify", dt, compressed_size);
  CHECK(success_count == NUM_BENCHMARK_ITERATIONS);
  CHECK(uncompressed_size2 == uncompressed_size);

  // Decompress the data.
  success_count = 0;
  t0 = get_time();
  for (int i = 0; i < NUM_BENCHMARK_ITERATIONS; ++i) {
    hzr_status_t status = hzr_decode(s_compressed, compressed_size,
                                     s_uncompressed2, uncompressed_size2);
    if (status == HZR_OK) {
      ++success_count;
    }
  }
  dt = get_time() - t0;
  print_results("Decode", dt, uncompressed_size);
  CHECK(success_count == NUM_BENCHMARK_ITERATIONS);

#ifdef HZR_HAS_ZLIB
  {
    t0 = get_time();
    for (int i = 0; i < NUM_BENCHMARK_ITERATIONS; ++i) {
      z_stream strm;
      strm.zalloc = Z_NULL;
      strm.zfree = Z_NULL;
      strm.opaque = Z_NULL;
      const int level = 5;
      if (deflateInit(&strm, level) == Z_OK) {
        strm.avail_in = uncompressed_size;
        strm.next_in = reinterpret_cast<Bytef*>(s_uncompressed);
        strm.avail_out = max_compressed_size;
        strm.next_out = reinterpret_cast<Bytef*>(s_compressed);
        deflate(&strm, Z_FINISH);
        compressed_size = max_compressed_size - strm.avail_out;
      }
      (void)deflateEnd(&strm);
    }
    dt = get_time() - t0;
    print_results("zlib encode", dt, uncompressed_size);

    t0 = get_time();
    for (int i = 0; i < NUM_BENCHMARK_ITERATIONS; ++i) {
      z_stream strm;
      strm.zalloc = Z_NULL;
      strm.zfree = Z_NULL;
      strm.opaque = Z_NULL;
      strm.avail_in = 0;
      strm.next_in = Z_NULL;
      if (inflateInit(&strm) == Z_OK) {
        strm.avail_in = compressed_size;
        strm.next_in = reinterpret_cast<Bytef*>(s_compressed);
        strm.avail_out = uncompressed_size;
        strm.next_out = reinterpret_cast<Bytef*>(s_uncompressed2);
        inflate(&strm, Z_FINISH);
      }
      (void)inflateEnd(&strm);
    }
    dt = get_time() - t0;
    print_results("zlib decode", dt, uncompressed_size);
  }
#endif  // HZR_HAS_ZLIB

  t0 = get_time();
  for (int i = 0; i < NUM_BENCHMARK_ITERATIONS; ++i) {
    std::memcpy(s_uncompressed2, s_uncompressed, uncompressed_size);
  }
  dt = get_time() - t0;
  print_results("memcpy (reference)", dt, uncompressed_size);
}

}  // namespace

TEST_CASE("Test 1 (all zeros)", "[performance]") {
  std::cout << "Test 1 (all zeros)" << std::endl;
  for (size_t k = 0; k < NUM_SIZES; ++k) {
    const size_t uncompressed_size = SIZES[k];
    std::fill(s_uncompressed, s_uncompressed + uncompressed_size, 0);
    perform_test(uncompressed_size);
  }
}

TEST_CASE("Test 2 (random)", "[performance]") {
  std::cout << "Test 2 (random)" << std::endl;
  for (size_t k = 0; k < NUM_SIZES; ++k) {
    const size_t uncompressed_size = SIZES[k];
    random_t random(1234);
    for (size_t i = 0; i < uncompressed_size; ++i) {
      s_uncompressed[i] = random.rnd();
    }
    perform_test(uncompressed_size);
  }
}

TEST_CASE("Test 3 (gaussian(8) + zeros)", "[performance]") {
  std::cout << "Test 3 (gaussian(8) + zeros)" << std::endl;
  for (size_t k = 0; k < NUM_SIZES; ++k) {
    const size_t uncompressed_size = SIZES[k];
    std::fill(s_uncompressed, s_uncompressed + uncompressed_size / 2, 0);
    random_t random(1234);
    for (size_t i = uncompressed_size / 2; i < uncompressed_size; ++i) {
      s_uncompressed[i] = random.gaussian(8);
    }
    perform_test(uncompressed_size);
  }
}

TEST_CASE("Test 4 (gaussian(2))", "[performance]") {
  std::cout << "Test 4 (gaussian(2))" << std::endl;
  for (size_t k = 0; k < NUM_SIZES; ++k) {
    const size_t uncompressed_size = SIZES[k];
    random_t random(1234);
    for (size_t i = 0; i < uncompressed_size; ++i) {
      s_uncompressed[i] = random.gaussian(2);
    }
    perform_test(uncompressed_size);
  }
}

TEST_CASE("Test 5 (all ones)", "[performance]") {
  std::cout << "Test 5 (all ones)" << std::endl;
  for (size_t k = 0; k < NUM_SIZES; ++k) {
    const size_t uncompressed_size = SIZES[k];
    std::fill(s_uncompressed, s_uncompressed + uncompressed_size, 1);
    perform_test(uncompressed_size);
  }
}
