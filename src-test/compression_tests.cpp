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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libhzr.h>

namespace {

const size_t max_uncompressed_size = 500000;

const size_t sizes[] = {
  max_uncompressed_size,
  max_uncompressed_size / 2,
  max_uncompressed_size / 5,
  max_uncompressed_size / 10,
  max_uncompressed_size / 20,
  max_uncompressed_size / 50,
  max_uncompressed_size > 100 ? 100 : max_uncompressed_size,
  max_uncompressed_size > 10 ? 10 : max_uncompressed_size,
  max_uncompressed_size > 1 ? 1 : max_uncompressed_size,
  0
};
const size_t num_sizes = sizeof(sizes) / sizeof(sizes[0]);

bool perform_test(const unsigned char* uncompressed,
                        size_t uncompressed_size) {
  // Compress the data.
  const size_t max_compressed_size = hzr_max_compressed_size(uncompressed_size);
  printf("  - Max compressed size: %ld\n", max_compressed_size);
  unsigned char *compressed = (unsigned char *)malloc(max_compressed_size);
  if (!compressed) {
    printf("  - Unable to allocate memory for uncompressed data.\n");
    return false;
  }
  size_t compressed_size;
  if (!hzr_encode(uncompressed, uncompressed_size, compressed,
                  max_compressed_size, &compressed_size)) {
    printf("  - Unable to compress the data.\n");
    free(compressed);
    return false;
  }
  printf("  - Compressed size: %ld (%f:1)\n", compressed_size,
         (double)uncompressed_size / (double)compressed_size);

  // Decompress the data.
  size_t uncompressed_size2;
  if (!hzr_verify(compressed, compressed_size, &uncompressed_size2)) {
    printf("  - Verification of the compressed data failed.\n");
    free(compressed);
    return false;
  }
  if (uncompressed_size2 != uncompressed_size) {
    printf("  - Decoded size mismatch: %ld != %ld\n", uncompressed_size2,
           uncompressed_size);
    free(compressed);
    return false;
  }
  unsigned char *uncompressed2 = (unsigned char *)malloc(uncompressed_size2);
  if (!uncompressed2) {
    printf("  - Unable to allocate memory for uncompressed data 2.\n");
    free(compressed);
    return false;
  }
  if (!hzr_decode(compressed, compressed_size, uncompressed2,
                  uncompressed_size2)) {
    printf("  - Unable to decode the data.\n");
    free(uncompressed2);
    free(compressed);
    return false;
  }

  // Check that the data is correct.
  if (memcmp(uncompressed, uncompressed2, uncompressed_size) != 0) {
    printf("  - The decoded data did not match the original data.\n");
    free(uncompressed2);
    free(compressed);
    return false;
  }

  // Free buffers.
  free(uncompressed2);
  free(compressed);

  return true;
}

void test_data(const char* name,
                     const unsigned char* uncompressed,
                     size_t uncompressed_size) {
  printf("TEST: %s (%ld bytes)\n", name, uncompressed_size);
  int result = perform_test(uncompressed, uncompressed_size);
  REQUIRE(result == true);
}

void test_data_1(unsigned char *uncompressed, size_t uncompressed_size) {
  memset(uncompressed, 0, uncompressed_size);
  test_data("good case (all zeros)", uncompressed, uncompressed_size);
}

void test_data_2(unsigned char *uncompressed, size_t uncompressed_size) {
  for (size_t i = 0; i < uncompressed_size; ++i) {
    uncompressed[i] = i & 255;
  }
  test_data("bad case", uncompressed, uncompressed_size);
}

void test_data_3(unsigned char *uncompressed, size_t uncompressed_size) {
  memset(uncompressed, 0, uncompressed_size);
  for (size_t i = uncompressed_size / 2; i < uncompressed_size; ++i) {
    uncompressed[i] = i & 255;
  }
  test_data("test3", uncompressed, uncompressed_size);
}

void test_data_4(unsigned char *uncompressed, size_t uncompressed_size) {
  for (size_t i = 0; i < uncompressed_size; ++i) {
    uncompressed[i] = i & 15;
  }
  test_data("test4", uncompressed, uncompressed_size);
}

void test_data_5(unsigned char *uncompressed, size_t uncompressed_size) {
  memset(uncompressed, 1, uncompressed_size);
  test_data("all ones", uncompressed, uncompressed_size);
}

} // namespace

TEST_CASE("Test everything", "[compression]" ) {
  // Allocate memory for the uncompressed data.
  unsigned char *uncompressed = (unsigned char *)malloc(max_uncompressed_size);
  if (!uncompressed) {
    printf("Unable to allocate memory for uncompressed data.\n");
    return;
  }

  // Perform tests.
  for (size_t k = 0; k < num_sizes; ++k) {
    const size_t uncompressed_size = sizes[k];
    test_data_1(uncompressed, uncompressed_size);
    test_data_2(uncompressed, uncompressed_size);
    test_data_3(uncompressed, uncompressed_size);
    test_data_4(uncompressed, uncompressed_size);
    test_data_5(uncompressed, uncompressed_size);
  }

  // Free the uncompressed data.
  free(uncompressed);
}

