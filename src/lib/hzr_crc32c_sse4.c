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

#include "hzr_crc32c_sse4.h"

// Check if we are compiling with SSE 4.2 support.
#if defined(__SSE4_2__) || (defined(_M_IX86_FP) && (_M_IX86_FP >= 2))

#include <nmmintrin.h>

#if defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
#endif
#if defined(_MSC_VER)
#include <intrin.h>
#endif

#define CPUID_VENDOR_ID 0x00000000
#define CPUID_FEATURES 0x00000001

// A fairly portable x86 cpuid() implementation.
static void cpuid(unsigned func,
                  unsigned* a,
                  unsigned* b,
                  unsigned* c,
                  unsigned* d) {
#if defined(__GNUC__) || defined(__clang__)
  __cpuid(func, *a, *b, *c, *d);
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

// Check if we can use SSE 4.2, at runtime.
hzr_bool _hzr_can_use_sse4_2(void) {
  unsigned a, b, c, d;
  cpuid(CPUID_VENDOR_ID, &a, &b, &c, &d);
  if (a >= CPUID_FEATURES) {
    cpuid(CPUID_FEATURES, &a, &b, &c, &d);
    return (c & (1U << 20)) ? HZR_TRUE : HZR_FALSE;
  }
  return HZR_FALSE;
}

// SSE 4.2 optimized CRC32 implementation.
uint32_t _hzr_crc32c_sse4_2(const uint8_t* buf, size_t size) {
  const size_t ALIGN_TO_BYTES = 8;

  uint32_t crc = ~0U;

  // Align...
  size_t align = (size_t)buf & (ALIGN_TO_BYTES - 1);
  if (align != 0U) {
    align = ALIGN_TO_BYTES - align;
    align = align > size ? size : align;
    size -= align;
    for (; align; --align) {
      crc = _mm_crc32_u8(crc, *buf++);
    }
  }

  // Use as big chunks as possible.
#if defined(__x86_64__) || defined(_M_X64)
  for (; size >= 8; size -= 8, buf += 8) {
    crc = (uint32_t)_mm_crc32_u64(crc, *(const uint64_t*)buf);
  }
#else
  for (; size >= 4; size -= 4, buf += 4) {
    crc = _mm_crc32_u32(crc, *(const uint32_t*)buf);
  }
#endif

  // Handle tail.
  while (size--) {
    crc = _mm_crc32_u8(crc, *buf++);
  }

  return ~crc;
}

#else

hzr_bool _hzr_can_use_sse4_2(void) {
  return HZR_FALSE;
}

uint32_t _hzr_crc32c_sse4_2(const uint8_t* buf, size_t size) {
  (void)buf;
  (void)size;
  return 0;
}

#endif  // SSE 4.2
