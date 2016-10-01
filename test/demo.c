#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libhzr.h>

int perform_test(const unsigned char *uncompressed, size_t uncompressed_size) {
  printf("  - Uncompressed size: %ld\n", uncompressed_size);

  // Compress the data.
  const size_t max_compressed_size = hzr_max_compressed_size(uncompressed_size);
  printf("  - Max compressed size: %ld\n", max_compressed_size);
  unsigned char *compressed = (unsigned char *)malloc(max_compressed_size);
  if (!compressed) {
    printf("  - Unable to allocate memory for uncompressed data.\n");
    return 0;
  }
  size_t compressed_size;
  if (!hzr_encode(uncompressed, uncompressed_size, compressed,
                  max_compressed_size, &compressed_size)) {
    printf("  - Unable to compress the data.\n");
    free(compressed);
    return 0;
  }
  printf("  - Compressed size: %ld (%f:1)\n", compressed_size,
         (float)uncompressed_size / (float)compressed_size);

  // Decompress the data.
  size_t uncompressed_size2;
  if (!hzr_verify(compressed, compressed_size, &uncompressed_size2)) {
    printf("  - Verification of the compressed data failed.\n");
    free(compressed);
    return 0;
  }
  printf("  - Uncompressed size 2: %ld\n", uncompressed_size2);
  if (uncompressed_size2 != uncompressed_size) {
    printf("  - ERROR: %ld != %ld\n", uncompressed_size2, uncompressed_size);
    free(compressed);
    return 0;
  }
  unsigned char *uncompressed2 = (unsigned char *)malloc(uncompressed_size2);
  if (!uncompressed2) {
    printf("  - Unable to allocate memory for uncompressed data 2.\n");
    free(compressed);
    return 0;
  }
  if (!hzr_decode(compressed, compressed_size, uncompressed2,
                  uncompressed_size2)) {
    printf("  - Unable to decode the data.\n");
    free(uncompressed2);
    free(compressed);
    return 0;
  }

  // Check that the data is correct.
  if (memcmp(uncompressed, uncompressed2, uncompressed_size) != 0) {
    printf("  - The decoded data did not match the original data.\n");
    free(uncompressed2);
    free(compressed);
    return 0;
  }

  // Free buffers.
  free(uncompressed2);
  free(compressed);

  return 1;
}

void test_data(const char* name, const unsigned char *uncompressed, size_t uncompressed_size) {
  printf("TEST: %s\n", name);
  if (perform_test(uncompressed, uncompressed_size)) {
    printf("SUCCESSFUL!\n");
  } else {
    printf("***FAILED***\n");
  }
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

int main() {
  // Allocate memory for the uncompressed data.
  const size_t uncompressed_size = 500000;
  unsigned char *uncompressed = (unsigned char *)malloc(uncompressed_size);
  if (!uncompressed) {
    printf("Unable to allocate memory for uncompressed data.\n");
    return 1;
  }

  // Perform tests.
  test_data_1(uncompressed, uncompressed_size);
  test_data_2(uncompressed, uncompressed_size);
  test_data_3(uncompressed, uncompressed_size);
  test_data_4(uncompressed, uncompressed_size);
  test_data_5(uncompressed, uncompressed_size);

  // Free the uncompressed data.
  free(uncompressed);

  return 0;
}
