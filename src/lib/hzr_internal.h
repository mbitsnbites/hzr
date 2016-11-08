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

// Debug macros.
#if !defined(NDEBUG)
#include <stdio.h>
#define DLOG(s) printf("%s:%d: %s\n", __FILE__, __LINE__, (s))
#else
#define DLOG(s)
#endif

// Min/max macros.
#define hzr_min(x, y) ((x) <= (y) ? (x) : (y))
#define hzr_max(x, y) ((x) >= (y) ? (x) : (y))

// Types.
typedef enum { HZR_FALSE = 0, HZR_TRUE = 1 } hzr_bool;

// Encoded data header size (in bytes).
// The header format is:
//  0: size of the decoded data (32 bits).
//  4: CRC32 of the encoded data (32 bits).
//  8: Encoding mode:
//     0 = Plain copy (no compression)
//     1 = Huffman + RLE
//     2 = Fill
#define HZR_HEADER_SIZE 9

#define HZR_ENCODING_COPY     0
#define HZR_ENCODING_HUFF_RLE 1
#define HZR_ENCODING_FILL     2
#define HZR_ENCODING_LAST     HZR_ENCODING_FILL

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
