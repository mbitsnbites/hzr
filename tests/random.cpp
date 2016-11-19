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

// From Wikipedia: https://en.wikipedia.org/wiki/Mersenne_Twister

#include "random.h"

#include <math.h>

namespace {

// Define MT19937 constants (32-bit RNG)

// Assumes W = 32 (omitting this)
const unsigned M = 397;
const unsigned R = 31;
const unsigned A = 0x9908B0DF;
const unsigned F = 1812433253;

const unsigned U = 11;
// Assumes D = 0xFFFFFFFF (omitting this)

const unsigned S = 7;
const unsigned B = 0x9D2C5680;

const unsigned T = 15;
const unsigned C = 0xEFC60000;

const unsigned L = 18;

const unsigned MASK_LOWER = (1ull << R) - 1;
const unsigned MASK_UPPER = 1ull << R;

}  // namespace

void random_t::twist() {
  uint32_t i, x, xA;

  for (i = 0; i < N; i++) {
    x = (mt_[i] & MASK_UPPER) + (mt_[(i + 1) % N] & MASK_LOWER);

    xA = x >> 1;

    if (x & 0x1)
      xA ^= A;

    mt_[i] = mt_[(i + M) % N] ^ xA;
  }

  index_ = 0;
}

double random_t::gaussian(const double mean, const double stdDev) {
  static int hasSpare = 0;
  static double spare;

  if (hasSpare) {
    hasSpare = 0;
    return mean + stdDev * spare;
  }
  hasSpare = 1;

  double u, v, s;
  do {
    u = ((double)get_u32() / 4294967296.0) * 2.0 - 1.0;
    v = ((double)get_u32() / 4294967296.0) * 2.0 - 1.0;
    s = u * u + v * v;
  } while ((s >= 1.0) || (s == 0.0));

  s = sqrt(-2.0 * log(s) / s);
  spare = v * s;
  return mean + stdDev * u * s;
}

// Re-init with a given seed
random_t::random_t(const uint32_t seed) {
  mt_[0] = seed;

  uint32_t i;
  for (i = 1; i < N; i++) {
    mt_[i] = (F * (mt_[i - 1] ^ (mt_[i - 1] >> 30)) + i);
  }

  index_ = N;
}

// Obtain a 32-bit random number
uint32_t random_t::get_u32() {
  uint32_t y;
  int i = index_;

  if (index_ >= N) {
    twist();
    i = index_;
  }

  y = mt_[i];
  index_ = i + 1;

  y ^= (mt_[i] >> U);
  y ^= (y << S) & B;
  y ^= (y << T) & C;
  y ^= (y >> L);

  return y;
}

// Obtain an 8-bit random number
uint8_t random_t::rnd() {
  uint32_t r32 = get_u32();
  return (uint8_t)((r32 >> 24) ^ (r32 >> 16) ^ (r32 >> 8) ^ r32);
}

uint8_t random_t::gaussian(uint8_t std_dev) {
  double g = gaussian(0.0, (double)std_dev);
  if (g < -128.0 || g > 127.0) {
    g = 0.0;
  }
  if (g < 0.0) {
    g = 255.0 + g;
  }
  return (uint8_t)(g + 0.5);
}
