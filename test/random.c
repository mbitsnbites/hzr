// From Wikipedia: https://en.wikipedia.org/wiki/Mersenne_Twister

#include "random.h"

// Define MT19937 constants (32-bit RNG)

// Assumes W = 32 (omitting this)
#define N 624
#define M 397
#define R 31
#define A 0x9908B0DF
#define F 1812433253

#define U 11
// Assumes D = 0xFFFFFFFF (omitting this)

#define S 7
#define B 0x9D2C5680

#define T 15
#define C 0xEFC60000

#define L 18

#define MASK_LOWER ((1ull << R) - 1)
#define MASK_UPPER ((1ull << R))

static uint32_t mt[N];
static uint16_t index;

static void Twist() {
  uint32_t i, x, xA;

  for (i = 0; i < N; i++) {
    x = (mt[i] & MASK_UPPER) + (mt[(i + 1) % N] & MASK_LOWER);

    xA = x >> 1;

    if (x & 0x1)
      xA ^= A;

    mt[i] = mt[(i + M) % N] ^ xA;
  }

  index = 0;
}

// Re-init with a given seed
void random_init(const uint32_t seed) {
  uint32_t i;

  mt[0] = seed;

  for (i = 1; i < N; i++) {
    mt[i] = (F * (mt[i - 1] ^ (mt[i - 1] >> 30)) + i);
  }

  index = N;
}

// Obtain a 32-bit random number
uint32_t random_get_u32() {
  uint32_t y;
  int i = index;

  if (index >= N) {
    Twist();
    i = index;
  }

  y = mt[i];
  index = i + 1;

  y ^= (mt[i] >> U);
  y ^= (y << S) & B;
  y ^= (y << T) & C;
  y ^= (y >> L);

  return y;
}
