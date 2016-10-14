#ifndef HZR_CRC32C_SSE4_H_
#define HZR_CRC32C_SSE4_H_

#include <stddef.h>
#include <stdint.h>

#include "hzr_internal.h"

#ifdef HZR_USE_SSE4_2
int _hzr_can_use_sse4_2();
uint32_t _hzr_crc32c_sse4_2(const uint8_t* buf, size_t size);
#endif

#endif /* HZR_CRC32C_SSE4_H_ */
