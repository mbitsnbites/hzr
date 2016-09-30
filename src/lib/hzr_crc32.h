#ifndef HZR_CRC32_H_
#define HZR_CRC32_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t _hzr_crc32(const void* data, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* HZR_CRC32_H_ */
