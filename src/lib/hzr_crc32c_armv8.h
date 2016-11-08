#ifndef HZR_CRC32C_ARMV8_H_
#define HZR_CRC32C_ARMV8_H_

#include <stddef.h>
#include <stdint.h>

#include "hzr_internal.h"

hzr_bool _hzr_can_use_armv8crc(void);
uint32_t _hzr_crc32c_armv8crc(const uint8_t* buf, size_t size);

#endif // HZR_CRC32C_ARMV8_H_
