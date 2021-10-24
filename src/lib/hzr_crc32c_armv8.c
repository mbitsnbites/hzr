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

#include "hzr_crc32c_armv8.h"

// Check if we are compiling with ARMv8+CRC32 support.
#ifdef __ARM_FEATURE_CRC32

#include <arm_acle.h>

#ifdef __linux__
#include <sys/auxv.h>

#ifndef HWCAP_CRC32
#define HWCAP_CRC32 (1U << 7)
#endif  // HWCAP_CRC32

// Check if we can use CRC32 extensions, at runtime.
hzr_bool _hzr_can_use_armv8crc(void) {
  return (getauxval(AT_HWCAP) & HWCAP_CRC32) ? HZR_TRUE : HZR_FALSE;
}

#else

// Assume that we can use CRC32 extensions if we're compiling with them
// enabled on a non-linux system.
hzr_bool _hzr_can_use_armv8crc(void) {
  return HZR_TRUE;
}

#endif  // __linux__

// ARMv8 + CRC32 optimized CRC32 implementation.
uint32_t _hzr_crc32c_armv8crc(const uint8_t* buf, size_t size) {
  const size_t ALIGN_TO_BYTES = 8;

  uint32_t crc = ~0U;

  // Align...
  size_t align = (size_t)buf & (ALIGN_TO_BYTES - 1);
  if (align != 0U) {
    align = ALIGN_TO_BYTES - align;
    align = align > size ? size : align;
    size -= align;
    for (; align; --align) {
      crc = __crc32cb(crc, *buf++);
    }
  }

  // Do eight bytes per iteration.
  for (; size >= 8; size -= 8, buf += 8) {
    crc = __crc32cd(crc, *(const uint64_t*)buf);
  }

  // Handle tail.
  while (size--) {
    crc = __crc32cb(crc, *buf++);
  }

  return ~crc;
}

#else

hzr_bool _hzr_can_use_armv8crc(void) {
  return HZR_FALSE;
}

uint32_t _hzr_crc32c_armv8crc(const uint8_t* buf, size_t size) {
  (void)buf;
  (void)size;
  return 0;
}

#endif  // __ARM_FEATURE_CRC32
