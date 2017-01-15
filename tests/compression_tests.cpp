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

#include <algorithm>
#include <iostream>

#include <libhzr.h>

namespace {

const size_t MAX_UNCOMPRESSED_SIZE = 500000;

const size_t SIZES[] = {
    MAX_UNCOMPRESSED_SIZE,
    MAX_UNCOMPRESSED_SIZE / 2,
    MAX_UNCOMPRESSED_SIZE / 5,
    MAX_UNCOMPRESSED_SIZE / 10,
    MAX_UNCOMPRESSED_SIZE / 20,
    MAX_UNCOMPRESSED_SIZE / 50,
    MAX_UNCOMPRESSED_SIZE > 100 ? 100 : MAX_UNCOMPRESSED_SIZE,
    MAX_UNCOMPRESSED_SIZE > 10 ? 10 : MAX_UNCOMPRESSED_SIZE,
    MAX_UNCOMPRESSED_SIZE > 1 ? 1 : MAX_UNCOMPRESSED_SIZE,
    0};
const size_t NUM_SIZES = sizeof(SIZES) / sizeof(SIZES[0]);

// This is an approximation (rounded up) of the maximum compressed size.
const size_t MAX_COMPRESSED_SIZE =
    MAX_UNCOMPRESSED_SIZE + (MAX_UNCOMPRESSED_SIZE >> 13) + 4;

// Statically allocate memory for the compression/decompression.
unsigned char s_uncompressed[MAX_UNCOMPRESSED_SIZE];
unsigned char s_compressed[MAX_COMPRESSED_SIZE];
unsigned char s_uncompressed2[MAX_UNCOMPRESSED_SIZE];

void perform_test(size_t uncompressed_size) {
  // Compress the data.
  const size_t max_compressed_size = hzr_max_compressed_size(uncompressed_size);
  size_t compressed_size;
  REQUIRE(hzr_encode(s_uncompressed, uncompressed_size, s_compressed,
                     max_compressed_size, &compressed_size));
  std::cout << "  Compression ratio: "
            << (static_cast<double>(uncompressed_size) /
                static_cast<double>(compressed_size)) << ":1 ("
            << uncompressed_size << ":" << compressed_size << ")" << std::endl;

  // Decompress the data.
  size_t uncompressed_size2;
  CHECK(hzr_verify(s_compressed, compressed_size, &uncompressed_size2));
  CHECK(uncompressed_size2 == uncompressed_size);
  CHECK(hzr_decode(s_compressed, compressed_size, s_uncompressed2,
                   uncompressed_size2));

  // Check that the data is correct.
  CHECK(std::equal(s_uncompressed, s_uncompressed + uncompressed_size,
                   s_uncompressed2));
}

}  // namespace

TEST_CASE("Test 1 (good case)", "[compression]") {
  std::cout << "Test 1 (good case)" << std::endl;
  for (size_t k = 0; k < NUM_SIZES; ++k) {
    const size_t uncompressed_size = SIZES[k];
    std::fill(s_uncompressed, s_uncompressed + uncompressed_size, 0);
    perform_test(uncompressed_size);
  }
}

TEST_CASE("Test 2 (bad case)", "[compression]") {
  std::cout << "Test 2 (bad case)" << std::endl;
  for (size_t k = 0; k < NUM_SIZES; ++k) {
    const size_t uncompressed_size = SIZES[k];
    for (size_t i = 0; i < uncompressed_size; ++i) {
      s_uncompressed[i] = i & 255;
    }
    perform_test(uncompressed_size);
  }
}

TEST_CASE("Test 3", "[compression]") {
  std::cout << "Test 3" << std::endl;
  for (size_t k = 0; k < NUM_SIZES; ++k) {
    const size_t uncompressed_size = SIZES[k];
    std::fill(s_uncompressed + (uncompressed_size / 2),
              s_uncompressed + uncompressed_size, 0);
    for (size_t i = uncompressed_size / 2; i < uncompressed_size; ++i) {
      s_uncompressed[i] = i & 255;
    }
    perform_test(uncompressed_size);
  }
}

TEST_CASE("Test 4", "[compression]") {
  std::cout << "Test 4" << std::endl;
  for (size_t k = 0; k < NUM_SIZES; ++k) {
    const size_t uncompressed_size = SIZES[k];
    for (size_t i = 0; i < uncompressed_size; ++i) {
      s_uncompressed[i] = i & 15;
    }
    perform_test(uncompressed_size);
  }
}

TEST_CASE("Test 5", "[compression]") {
  std::cout << "Test 5" << std::endl;
  for (size_t k = 0; k < NUM_SIZES; ++k) {
    const size_t uncompressed_size = SIZES[k];
    std::fill(s_uncompressed, s_uncompressed + uncompressed_size, 1);
    perform_test(uncompressed_size);
  }
}
