#ifndef HZR_INTERNAL_H_
#define HZR_INTERNAL_H_

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

#endif  // HZR_INTERNAL_H_
