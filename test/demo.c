#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libhzr.h>

int perform_test(const unsigned char *uncompressed, size_t uncompressed_size) {
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
  if (uncompressed_size2 != uncompressed_size) {
    printf("  - Decoded size mismatch: %ld != %ld\n", uncompressed_size2, uncompressed_size);
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

int test_data(const char *name, const unsigned char *uncompressed,
               size_t uncompressed_size) {
  printf("TEST: %s (%ld bytes)\n", name, uncompressed_size);
  int result = perform_test(uncompressed, uncompressed_size);
  if (result) {
    printf("SUCCESSFUL!\n");
  } else {
    printf("***FAILED***\n");
  }
  return result;
}

int test_data_1(unsigned char *uncompressed, size_t uncompressed_size) {
  memset(uncompressed, 0, uncompressed_size);
  return test_data("good case (all zeros)", uncompressed, uncompressed_size);
}

int test_data_2(unsigned char *uncompressed, size_t uncompressed_size) {
  for (size_t i = 0; i < uncompressed_size; ++i) {
    uncompressed[i] = i & 255;
  }
  return test_data("bad case", uncompressed, uncompressed_size);
}

int test_data_3(unsigned char *uncompressed, size_t uncompressed_size) {
  memset(uncompressed, 0, uncompressed_size);
  for (size_t i = uncompressed_size / 2; i < uncompressed_size; ++i) {
    uncompressed[i] = i & 255;
  }
  return test_data("test3", uncompressed, uncompressed_size);
}

int test_data_4(unsigned char *uncompressed, size_t uncompressed_size) {
  for (size_t i = 0; i < uncompressed_size; ++i) {
    uncompressed[i] = i & 15;
  }
  return test_data("test4", uncompressed, uncompressed_size);
}

int test_data_5(unsigned char *uncompressed, size_t uncompressed_size) {
  memset(uncompressed, 1, uncompressed_size);
  return test_data("all ones", uncompressed, uncompressed_size);
}

int main() {
  // Allocate memory for the uncompressed data.
  const size_t max_uncompressed_size = 500000;
  unsigned char *uncompressed = (unsigned char *)malloc(max_uncompressed_size);
  if (!uncompressed) {
    printf("Unable to allocate memory for uncompressed data.\n");
    return 1;
  }

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

  // Perform tests.
  int success_count = 0, total_count = 0;
  for (size_t k = 0; k < num_sizes; ++k) {
    const size_t uncompressed_size = sizes[k];
    success_count += test_data_1(uncompressed, uncompressed_size);
    success_count += test_data_2(uncompressed, uncompressed_size);
    success_count += test_data_3(uncompressed, uncompressed_size);
    success_count += test_data_4(uncompressed, uncompressed_size);
    success_count += test_data_5(uncompressed, uncompressed_size);
    total_count += 5;
  }

  // Print summary.
  printf("\n%d tests: %d successful, %d fails\n",
         total_count, success_count, total_count - success_count);

  // Free the uncompressed data.
  free(uncompressed);

  return 0;
}
