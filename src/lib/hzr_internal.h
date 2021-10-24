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

#ifndef HZR_INTERNAL_H_
#define HZR_INTERNAL_H_

#include <stdint.h>

// Branch optimization macros. Use these sparingly! The most useful and obvious
// situations where these should be used are in error handling code (e.g. it's
// unlikely that input data is corrupt, so we can safely optimize for the
// expected code path).
#if defined(__GNUC__)
#define LIKELY(expr) __builtin_expect(!!(expr), 1)
#define UNLIKELY(expr) __builtin_expect(!!(expr), 0)
#else
#define LIKELY(expr) (expr)
#define UNLIKELY(expr) (expr)
#endif

// Inlining macros.
#if defined(__GNUC__)
#define FORCE_INLINE inline __attribute__((always_inline))
#else
#define FORCE_INLINE
#endif

// Macros that are only enabled in debug mode.
#if !defined(NDEBUG)
#include <stdio.h>

// Logging macros.
#define DLOG(s) printf("%s:%d: %s\n", __FILE__, __LINE__, (s))
#define DLOGF(s, ...) \
  printf("%s:%d: " s "\n", __FILE__, __LINE__, ##__VA_ARGS__)

// Assert macros.
#ifdef _MSC_VER
#define break_into_debugger __debugbreak
#else
#define break_into_debugger __builtin_trap
#endif
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define ASSERT(x)                                                            \
  do {                                                                       \
    if (UNLIKELY(!(x))) {                                                    \
      printf(__FILE__ ":%d: Assertion failed: " TOSTRING(x) "\n", __LINE__); \
      break_into_debugger();                                                 \
    }                                                                        \
  } while (0)
#else
#define DLOG(s)
#define DLOGF(s, ...)
#define ASSERT(x)
#endif

// Min/max macros.
#define hzr_min(x, y) ((x) <= (y) ? (x) : (y))
#define hzr_max(x, y) ((x) >= (y) ? (x) : (y))

// Types.
typedef enum { HZR_FALSE = 0, HZR_TRUE = 1 } hzr_bool;

// The HZR data format is as follows:
// * A master header:
//    0: Size of the decoded data (32 bits).
//
// * Blocks, each representing a decompressed size of a maximum of 65536 bytes,
//   and each having the following header:
//    0: Size of the encoded data - 1 (16 bits).
//    2: CRC32 of the encoded data (32 bits).
//    6: Encoding mode (8 bits):
//       0 = Plain copy (no compression)
//       1 = Huffman + RLE
//       2 = Fill

// Size of the master header (in bytes).
#define HZR_HEADER_SIZE 4

// Size of the block header (in bytes).
#define HZR_BLOCK_HEADER_SIZE 7

#define HZR_ENCODING_COPY 0
#define HZR_ENCODING_HUFF_RLE 1
#define HZR_ENCODING_FILL 2
#define HZR_ENCODING_LAST HZR_ENCODING_FILL

// Maximum number of decoded bytes in a block.
#define HZR_MAX_BLOCK_SIZE 65536

// A symbol is a 9-bit unsigned number.
typedef uint16_t Symbol;
#define kSymbolSize 9
#define kNumSymbols 261

// Special symbols for RLE.
#define kSymTwoZeros 256        // 2            (0 bits)
#define kSymUpTo6Zeros 257      // 3 - 6        (2 bits)
#define kSymUpTo22Zeros 258     // 7 - 22       (4 bits)
#define kSymUpTo278Zeros 259    // 23 - 278     (8 bits)
#define kSymUpTo16662Zeros 260  // 279 - 16662  (14 bits)

// The maximum number of nodes in the Huffman tree (branch nodes + leaf nodes).
#define kMaxTreeNodes ((kNumSymbols * 2) - 1)

#endif  // HZR_INTERNAL_H_
