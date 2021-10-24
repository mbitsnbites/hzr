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

#ifndef HZR_CRC32C_ARMV8_H_
#define HZR_CRC32C_ARMV8_H_

#include <stddef.h>
#include <stdint.h>

#include "hzr_internal.h"

hzr_bool _hzr_can_use_armv8crc(void);
uint32_t _hzr_crc32c_armv8crc(const uint8_t* buf, size_t size);

#endif  // HZR_CRC32C_ARMV8_H_
