#include "hzr_crc32c_sse4.h"

#include "hzr_internal.h"

/* Check if we are compiling with ARMv8+CRC32 support. */
#ifdef __ARM_FEATURE_CRC32

#include <arm_acle.h>

#ifdef __linux__
#include <sys/auxv.h>

#ifndef HWCAP_CRC32
#define HWCAP_CRC32 (1U << 7)
#endif /* HWCAP_CRC32 */

/* Check if we can use CRC32 extensions, at runtime. */
int _hzr_can_use_armv8crc(void) {
  return (getauxval(AT_HWCAP) & HWCAP_CRC32) ? 1 : 0;
}

#else

/* Assume that we can use CRC32 extensions if we're compiling with them
 * enabled on a non-linux system. */
int _hzr_can_use_armv8crc(void) {
  return 1;
}

#endif /* __linux__ */


/* ARMv8 + CRC32 optimized CRC32 implementation. */
uint32_t _hzr_crc32c_armv8crc(const uint8_t* buf, size_t size) {
  const size_t ALIGN_TO_BYTES = 8;

  uint32_t crc = ~0U;

  /* Align... */
  size_t align = (size_t)buf & (ALIGN_TO_BYTES - 1);
  if (align != 0U) {
    align = ALIGN_TO_BYTES - align;
    align = align > size ? size : align;
    size -= align;
    for (; align; --align) {
      crc = __crc32cb(crc, *buf++);
    }
  }

  /* Do eight bytes per iteration. */
  for (; size >= 8; size -= 8, buf += 8) {
    crc = __crc32cd(crc, *(const uint64_t*)buf);
  }

  /* Handle tail. */
  while (size--) {
    crc = __crc32cb(crc, *buf++);
  }

  return ~crc;
}

#else

int _hzr_can_use_armv8crc(void) {
  return 0;
}

uint32_t _hzr_crc32c_armv8crc(const uint8_t* buf, size_t size) {
  (void)buf;
  (void)size;
  return 0;
}

#endif /* __ARM_FEATURE_CRC32 */
