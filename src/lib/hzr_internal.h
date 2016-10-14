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

// Debug macros.
#if !defined(NDEBUG)
#if defined(_MSC_VER)
#include <intrin.h>
#define DEBUG_BREAK() __debugbreak()
#else
#include <signal.h>
#define DEBUG_BREAK() raise(SIGTRAP)
#endif
#include <stdio.h>
#define DLOG(s, ...) \
  printf("%s:%d: %s\n", __FILE__, __LINE__, (s), ##__VA_ARGS__)
#define DBREAK(s, ...)      \
  do {                      \
    DLOG(s, ##__VA_ARGS__); \
    DEBUG_BREAK();          \
  } while (0)
#else
#define DEBUG_BREAK()
#define DLOG(s, ...)
#define DBREAK(s, ...)
#endif

// Min/max macros.
#define hzr_min(x, y) ((x) <= (y) ? (x) : (y))
#define hzr_max(x, y) ((x) >= (y) ? (x) : (y))

// Types.
enum hzr_bool {
  HZR_FALSE = 0,
  HZR_TRUE = 1
};

// Encoded data header size (in bytes).
// The header format is:
//  0: size of the decoded data (32 bits).
//  4: CRC32 of the encoded data (32 bits).
#define HZR_HEADER_SIZE 8

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

// Can we use SSE4.2?
#if !defined(HZR_USE_SSE4_2) && \
    (defined(__SSE4_2__) || defined(_M_IX86_FP) && (_M_IX86_FP >= 2))
#define HZR_USE_SSE4_2
#endif

#endif  // HZR_INTERNAL_H_
