/*
 *  hzr - A Huffman + RLE compression library.
 *
 * Copyright (C) 2016 Marcus Geelnard
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from the
 * use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 *  1. The origin of this software must not be misrepresented; you must not
 *     claim that you wrote the original software. If you use this software in
 *     a product, an acknowledgment in the product documentation would be
 *     appreciated but is not required.
 *  2. Altered source versions must be plainly marked as such, and must not be
 *     misrepresented as being the original software.
 *  3. This notice may not be removed or altered from any source distribution.
 *
 */

#ifndef LIBHZR_H_
#define LIBHZR_H_

#include <stddef.h> /* For size_t */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Return value for many libhzr functions.
 */
typedef enum {
  HZR_FAIL = 0, /**< Failure (zero). */
  HZR_OK = 1    /**< Success (non-zero). */
} hzr_status_t;

/**
 * @brief Determine the maximum (worst case) size of an HZR encoded buffer.
 * @param uncompressed_size Size of the uncompressed buffer in bytes.
 * @returns The maximum size (in bytes) of the compressed buffer.
 */
size_t hzr_max_compressed_size(size_t uncompressed_size);

/**
 * @brief Compress a buffer using the HZR compression scheme.
 * @param in Input (uncompressed) buffer.
 * @param in_size Size of the input buffer in bytes.
 * @param[out] out Output (compressed) buffer.
 * @param out_size Size of the output buffer in bytes.
 * @param[out] encoded_size Size of the encoded data in bytes.
 * @returns HZR_OK on success, else HZR_FAIL.
 */
hzr_status_t hzr_encode(const void* in,
                        size_t in_size,
                        void* out,
                        size_t out_size,
                        size_t* encoded_size);

/**
 * @brief Verify that a buffer is a valid HZR encoded buffer.
 * @param in Input (compressed) buffer.
 * @param in_size Size of the input buffer in bytes.
 * @param[out] decoded_size Size of the decoded data in bytes.
 * @returns HZR_OK on success, else HZR_FAIL.
 *
 * If the provided buffer is a valid HZR encoded buffer, the size of the decoded
 * buffer is returned in decoded_size.
 */
hzr_status_t hzr_verify(const void* in, size_t in_size, size_t* decoded_size);

/**
 * @brief Decode an HZR encoded buffer.
 * @param in Input (compressed) buffer.
 * @param in_size Size of the input buffer in bytes.
 * @param[out] out Output (uncompressed) buffer.
 * @param out_size Size of the output buffer in bytes.
 * @returns HZR_OK on success, else HZR_FAIL.
 * @note It is expected that the input buffer is a valid HZR encoded buffer,
 * which should be verified by calling hzr_verify() first.
 */
hzr_status_t hzr_decode(const void* in,
                        size_t in_size,
                        void* out,
                        size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* LIBHZR_H_ */
