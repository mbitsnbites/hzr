#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/time.h>
#endif

#include <libhzr.h>

#ifdef HZR_HAS_ZLIB
#include <zlib.h>
#endif // HZR_HAS_ZLIB

#include "random.h"

static double get_time() {
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

static const int NUM_BENCHMARK_ITERATIONS = 1000;

static void print_results(const char* str, double dt, size_t num_bytes) {
  double speed = (double)(NUM_BENCHMARK_ITERATIONS * num_bytes) / dt;
  printf("  %s: %.2f MB/s\n", str, speed / (1024.0 * 1024.0));
}

static int perform_test(const unsigned char* uncompressed,
                        size_t uncompressed_size) {
  double t0, dt;

  // Allocate a buffer for the compressed data.
  // NOTE: Must support both HZR and zlib compression!
  const size_t max_compressed_size = hzr_max_compressed_size(uncompressed_size);
  unsigned char *compressed = (unsigned char *)malloc(max_compressed_size);
  if (!compressed) {
    printf("  - Unable to allocate memory for compressed data.\n");
    return 0;
  }

  // Compress the data.
  t0 = get_time();
  size_t compressed_size = 0;
  for (int i = 0; i < NUM_BENCHMARK_ITERATIONS; ++i) {
    if (!hzr_encode(uncompressed, uncompressed_size, compressed,
                    max_compressed_size, &compressed_size)) {
      printf("  - Unable to compress the data.\n");
      free(compressed);
      return 0;
    }
  }
  dt = get_time() - t0;
  print_results("Encode", dt, uncompressed_size);

  // Verify the compressed data.
  size_t uncompressed_size2 = 0;
  t0 = get_time();
  for (int i = 0; i < NUM_BENCHMARK_ITERATIONS; ++i) {
    if (!hzr_verify(compressed, compressed_size, &uncompressed_size2)) {
      printf("  - Verification of the compressed data failed.\n");
      free(compressed);
      return 0;
    }
  }
  dt = get_time() - t0;
  print_results("Verify", dt, compressed_size);

  if (uncompressed_size2 != uncompressed_size) {
    printf("  - Decoded size mismatch: %ld != %ld\n", uncompressed_size2, uncompressed_size);
    free(compressed);
    return 0;
  }

  // Allocate memory for the uncompressed data.
  unsigned char *uncompressed2 = (unsigned char *)malloc(uncompressed_size2);
  if (!uncompressed2) {
    printf("  - Unable to allocate memory for uncompressed data 2.\n");
    free(compressed);
    return 0;
  }

  // Decompress the data.
  t0 = get_time();
  for (int i = 0; i < NUM_BENCHMARK_ITERATIONS; ++i) {
    if (!hzr_decode(compressed, compressed_size, uncompressed2,
                    uncompressed_size2)) {
      printf("  - Unable to decode the data.\n");
      free(uncompressed2);
      free(compressed);
      return 0;
    }
  }
  dt = get_time() - t0;
  print_results("Decode", dt, uncompressed_size);

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
        strm.next_in = (Bytef*)uncompressed;
        strm.avail_out = max_compressed_size;
        strm.next_out = (Bytef*)compressed;
        deflate(&strm, Z_FINISH);
        compressed_size = max_compressed_size - strm.avail_out;
      }
      deflateEnd(&strm);
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
        strm.next_in = (Bytef*)compressed;
        strm.avail_out = uncompressed_size;
        strm.next_out = (Bytef*)uncompressed2;
        inflate(&strm, Z_FINISH);
      }
      (void)inflateEnd(&strm);
    }
    dt = get_time() - t0;
    print_results("zlib decode", dt, uncompressed_size);
  }
#endif // HZR_HAS_ZLIB

  t0 = get_time();
  for (int i = 0; i < NUM_BENCHMARK_ITERATIONS; ++i) {
    memcpy(uncompressed2, uncompressed, uncompressed_size);
  }
  dt = get_time() - t0;
  print_results("memcpy (reference)", dt, uncompressed_size);

  // Free buffers.
  free(uncompressed2);
  free(compressed);

  return 1;
}

static int test_data(const char* name,
                     const unsigned char* uncompressed,
                     size_t uncompressed_size) {
  printf("CASE: %s (%ld bytes)\n", name, uncompressed_size);
  int result = perform_test(uncompressed, uncompressed_size);
  return result;
}

static int test_data_1(unsigned char *uncompressed, size_t uncompressed_size) {
  memset(uncompressed, 0, uncompressed_size);
  return test_data("good case (all zeros)", uncompressed, uncompressed_size);
}

static int test_data_2(unsigned char *uncompressed, size_t uncompressed_size) {
  random_init(1234);
  for (size_t i = 0; i < uncompressed_size; ++i) {
    uncompressed[i] = random_get_u8();
  }
  return test_data("bad case (rnd)", uncompressed, uncompressed_size);
}

static int test_data_3(unsigned char *uncompressed, size_t uncompressed_size) {
  random_init(1234);
  memset(uncompressed, 0, uncompressed_size);
  for (size_t i = uncompressed_size / 2; i < uncompressed_size; ++i) {
    uncompressed[i] = random_get_u8();
  }
  return test_data("rnd + zeros", uncompressed, uncompressed_size);
}

static int test_data_4(unsigned char *uncompressed, size_t uncompressed_size) {
  random_init(1234);
  for (size_t i = 0; i < uncompressed_size; ++i) {
    uncompressed[i] = random_get_u8() >> 4;
  }
  return test_data("rnd 4-bit", uncompressed, uncompressed_size);
}

static int test_data_5(unsigned char *uncompressed, size_t uncompressed_size) {
  memset(uncompressed, 1, uncompressed_size);
  return test_data("all ones", uncompressed, uncompressed_size);
}

int main() {
  random_init(1234);

  // Allocate memory for the uncompressed data.
  const size_t max_uncompressed_size = 131072;
  unsigned char *uncompressed = (unsigned char *)malloc(max_uncompressed_size);
  if (!uncompressed) {
    printf("Unable to allocate memory for uncompressed data.\n");
    return 1;
  }

  const size_t sizes[] = {
    max_uncompressed_size,
    max_uncompressed_size / 4,
    max_uncompressed_size / 8,
    max_uncompressed_size / 32,
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

