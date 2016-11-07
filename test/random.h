#ifndef RANDOM_H_
#define RANDOM_H_

#include <stdint.h>

// Initialize the random number generator.
void random_init(const uint32_t seed);

// Obtain a 32-bit random number.
uint32_t random_get_u32();

// Obtain an 8-bit random number.
uint8_t random_get_u8();

// Obtain an 8-bit normally distributed random number.
uint8_t gaussian_get_u8(uint8_t std_dev);

#endif // RANDOM_H_

