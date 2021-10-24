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

#ifndef RANDOM_H_
#define RANDOM_H_

#include <stdint.h>

class random_t {
public:
  random_t(const uint32_t seed);

  // Obtain an 8-bit random number.
  uint8_t rnd();

  // Obtain an 8-bit normally distributed random number.
  uint8_t gaussian(uint8_t std_dev);

private:
  void twist();
  double gaussian(const double mean, const double stdDev);
  uint32_t get_u32();

  static const int N = 624;

  uint32_t mt_[N];
  uint16_t index_;
};

#endif // RANDOM_H_
