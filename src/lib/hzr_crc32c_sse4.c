#include "hzr_crc32c_sse4.h"

#ifdef HZR_USE_SSE4_2

#include <nmmintrin.h>
#if defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
#endif
#if defined(_MSC_VER)
#include <intrin.h>
#endif

#define CPUID_VENDOR_ID 0x00000000
#define CPUID_FEATURES 0x00000001

/* A fairly portable x86 cpuid() implementation. */
static void cpuid(unsigned func,
                  unsigned* a,
                  unsigned* b,
                  unsigned* c,
                  unsigned* d) {
#if defined(__GNUC__) || defined(__clang__)
  __get_cpuid(func, a, b, c, d);
#elif defined(_MSC_VER)
  int info[4];
  __cpuid(info, (int)func);
  *a = (unsigned)info[0];
  *b = (unsigned)info[1];
  *c = (unsigned)info[2];
  *d = (unsigned)info[3];
#else
  (void)func;
  *a = *b = *c = *d = 0;
#endif
}

/* Check if we can use SSE 4.2, at runtime. */
int _hzr_can_use_sse4_2() {
  unsigned a, b, c, d;
  cpuid(CPUID_VENDOR_ID, &a, &b, &c, &d);
  if (a >= CPUID_FEATURES) {
    cpuid(CPUID_FEATURES, &a, &b, &c, &d);
    return (c & (1 << 20)) != 0;
  }
  return 0;
}

/* SSE 4.2 optimized CRC32 implementation. */
uint32_t _hzr_crc32c_sse4_2(const uint8_t* buf, size_t size) {
  const size_t ALIGN_TO_BYTES = 8;

  uint32_t crc = ~0U;

  /* Align... */
  for (; (size > 0) && ((size_t)buf & (ALIGN_TO_BYTES - 1)); size--) {
    crc = _mm_crc32_u8(crc, *buf++);
  }

  /* Use as big chunks as possible. */
#if defined(__x86_64__) || defined(_M_X64)
  for (; size >= 8; size -= 8, buf += 8) {
    crc = _mm_crc32_u64(crc, *(uint64_t*)buf);
  }
#else
  for (; size >= 4; size -= 4, buf += 4) {
    crc = _mm_crc32_u32(crc, *(uint32_t*)buf);
  }
#endif

  /* Handle tail. */
  while (size--) {
    crc = _mm_crc32_u8(crc, *buf++);
  }

  return ~crc;
}

#endif /* HZR_USE_SSE4_2 */
