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

#include "libhzr.h"

#include <stdint.h>
#include <string.h>

#include "hzr_crc32c.h"
#include "hzr_internal.h"

// A helper for decoding binary data.
typedef struct {
  const uint8_t* byte_ptr;
  const uint8_t* end_ptr;
  int bit_pos;
  uint32_t bit_cache;
  hzr_bool read_failed;
} ReadStream;

// Initialize a bitstream.
static void InitReadStream(ReadStream* stream, const void* buf, size_t size) {
  stream->byte_ptr = (const uint8_t*)buf;
  stream->end_ptr = ((const uint8_t*)buf) + size;
  stream->bit_pos = 0;
  stream->read_failed = HZR_FALSE;

  // Pre-fill the bit cache.
  stream->bit_cache = 0U;
  for (size_t i = 0; i < 4 && i < size; ++i) {
    stream->bit_cache |= ((uint32_t)stream->byte_ptr[i]) << (8 * i);
  }
}

static void ReInitBitCache(ReadStream* stream) {
  if (UNLIKELY(stream->bit_pos != 0)) {
    DLOGF("Unexpected bit position: %d (should be zero)", stream->bit_pos);
    stream->read_failed = HZR_TRUE;
    return;
  }

  stream->bit_cache = 0U;
  size_t bytes_left = (size_t)(stream->end_ptr - stream->byte_ptr);
  size_t bytes_to_read = hzr_min(4, bytes_left);
  for (size_t i = 0; i < bytes_to_read; ++i) {
    stream->bit_cache |= ((uint32_t)stream->byte_ptr[i]) << (8 * i);
  }
}

// Copy the read state from one stream to another without altering the end
// pointer.
static void CopyReadState(ReadStream* stream, const ReadStream* src_stream) {
  stream->byte_ptr = src_stream->byte_ptr;
  stream->bit_pos = src_stream->bit_pos;
  stream->bit_cache = src_stream->bit_cache;
  stream->read_failed = src_stream->read_failed;
}

FORCE_INLINE static const uint8_t* GetBytePtr(ReadStream* stream) {
  return stream->byte_ptr + (stream->bit_pos >> 3);
}

FORCE_INLINE static void UpdateBitCache(ReadStream* stream) {
  while (stream->bit_pos >= 8) {
    stream->bit_cache =
        (stream->bit_cache >> 8) | (((uint32_t)stream->byte_ptr[4]) << 24);
    stream->bit_pos -= 8;
    stream->byte_ptr++;
  }
}

FORCE_INLINE static void UpdateBitCacheSafe(ReadStream* stream) {
  while (stream->bit_pos >= 8) {
    stream->bit_cache >>= 8;
    if ((stream->byte_ptr + 4) < stream->end_ptr) {
      stream->bit_cache |= ((uint32_t)stream->byte_ptr[4]) << 24;
    }
    stream->byte_ptr++;
    stream->bit_pos -= 8;
  }
}

// Read one bit from a bitstream.
FORCE_INLINE static int ReadBit(ReadStream* stream) {
  int x = (stream->bit_cache >> stream->bit_pos) & 1;
  stream->bit_pos++;
  UpdateBitCache(stream);
  return x;
}

// Read one bit from a bitstream, with checking.
FORCE_INLINE static int ReadBitChecked(ReadStream* stream) {
  // Check that we don't read past the end.
  if (UNLIKELY(stream->byte_ptr >= stream->end_ptr)) {
    stream->read_failed = HZR_TRUE;
    return 0;
  }

  // Ok, read...
  int x = (stream->bit_cache >> stream->bit_pos) & 1;
  stream->bit_pos++;
  UpdateBitCacheSafe(stream);
  return x;
}

static const uint32_t s_bits_mask[33] = {
    0,  // Index zero is never used, since the index is in the range [1,32].
    0x00000001U, 0x00000003U, 0x00000007U, 0x0000000fU, 0x0000001fU,
    0x0000003fU, 0x0000007fU, 0x000000ffU, 0x000001ffU, 0x000003ffU,
    0x000007ffU, 0x00000fffU, 0x00001fffU, 0x00003fffU, 0x00007fffU,
    0x0000ffffU, 0x0001ffffU, 0x0003ffffU, 0x0007ffffU, 0x000fffffU,
    0x001fffffU, 0x003fffffU, 0x007fffffU, 0x00ffffffU, 0x01ffffffU,
    0x03ffffffU, 0x07ffffffU, 0x0fffffffU, 0x1fffffffU, 0x3fffffffU,
    0x7fffffffU, 0xffffffffU};

// Read multiple bits from a bitstream.
FORCE_INLINE static uint32_t ReadBits(ReadStream* stream, int bits) {
  // Read up to 32 bits at once.
  int bits_to_read = hzr_min(32 - stream->bit_pos, bits);
  uint32_t x =
      (stream->bit_cache >> stream->bit_pos) & s_bits_mask[bits_to_read];
  stream->bit_pos += bits_to_read;
  bits -= bits_to_read;
  UpdateBitCache(stream);

  // In the very unlikely case that we didn't get all the bits in the first
  // pass, we have to do a second pass (the caller has to request *at least*
  // 24 bits at once for this to ever happen).
  if (UNLIKELY(bits > 0)) {
    x |= (stream->bit_cache & s_bits_mask[bits]) << bits_to_read;
    stream->bit_pos += bits;
    UpdateBitCache(stream);
  }

  return x;
}

// Read multiple bits from a bitstream, with checking.
FORCE_INLINE static uint32_t ReadBitsChecked(ReadStream* stream, int bits) {
  // Check that we don't read past the end.
  int new_bit_pos = stream->bit_pos + bits;
  const uint8_t* new_byte_ptr = stream->byte_ptr + (new_bit_pos >> 3);
  if (UNLIKELY(new_byte_ptr > stream->end_ptr ||
               (new_byte_ptr == stream->end_ptr && ((new_bit_pos & 7) != 0)))) {
    stream->read_failed = HZR_TRUE;
    return 0;
  }

  // Ok, read up to 32 bits at once.
  int bits_to_read = hzr_min(32 - stream->bit_pos, bits);
  uint32_t x =
      (stream->bit_cache >> stream->bit_pos) & s_bits_mask[bits_to_read];
  stream->bit_pos += bits_to_read;
  bits -= bits_to_read;
  UpdateBitCacheSafe(stream);

  // In the very unlikely case that we didn't get all the bits in the first
  // pass, we have to do a second pass (the caller has to request *at least*
  // 24 bits at once for this to ever happen).
  if (UNLIKELY(bits > 0)) {
    x |= (stream->bit_cache & s_bits_mask[bits]) << bits_to_read;
    stream->bit_pos += bits;
    UpdateBitCacheSafe(stream);
  }

  return x;
}

// Peek eight bits from a bitstream (read without advancing the pointer).
FORCE_INLINE static uint8_t Peek8Bits(const ReadStream* stream) {
  return (uint8_t)(stream->bit_cache >> stream->bit_pos);
}

// Advance the pointer by N bits.
FORCE_INLINE static void Advance(ReadStream* stream, int N) {
  stream->bit_pos += N;
  UpdateBitCache(stream);
}

// Advance the pointer by N bits, with checking.
FORCE_INLINE static void AdvanceChecked(ReadStream* stream, int N) {
  int new_bit_pos = stream->bit_pos + N;
  const uint8_t* new_byte_ptr = stream->byte_ptr + (new_bit_pos >> 3);

  // Check that we don't advance past the end.
  if (UNLIKELY(new_byte_ptr > stream->end_ptr ||
               (new_byte_ptr == stream->end_ptr && ((new_bit_pos & 7) != 0)))) {
    stream->read_failed = HZR_TRUE;
    return;
  }

  // Ok, advance...
  stream->bit_pos = new_bit_pos;
  UpdateBitCacheSafe(stream);
}

// Advance the pointer by N bytes, with checking.
FORCE_INLINE static void AdvanceBytesChecked(ReadStream* stream, size_t N) {
  const uint8_t* new_byte_ptr = stream->byte_ptr + N;

  // We only allow this operation for aligned byte positions, and we do not
  // allow advancing past the end.
  if (UNLIKELY((stream->bit_pos != 0) || (new_byte_ptr > stream->end_ptr))) {
    stream->read_failed = HZR_TRUE;
    return;
  }

  // Advance.
  stream->byte_ptr = new_byte_ptr;

  // Re-populate the bit cache.
  ReInitBitCache(stream);
}

// Check if we have reached the end of the buffer.
FORCE_INLINE static hzr_bool AtTheEnd(const ReadStream* stream) {
  // This is a rought estimate that we have reached the end of the input
  // buffer (not too short, and not too far).
  return ((stream->byte_ptr == stream->end_ptr && stream->bit_pos == 0) ||
          (stream->byte_ptr == (stream->end_ptr - 1) && stream->bit_pos > 0))
             ? HZR_TRUE
             : HZR_FALSE;
}

typedef struct DecodeNode_struct DecodeNode;
struct DecodeNode_struct {
  DecodeNode* child_a;
  DecodeNode* child_b;
  int symbol;
};

typedef struct {
  DecodeNode* node;
  int symbol;
  int bits;
} DecodeLutEntry;

typedef struct {
  DecodeNode nodes[kMaxTreeNodes];
  DecodeLutEntry decode_lut[256];
} DecodeTree;

// Recursively recover a Huffman tree from a bitstream.
static DecodeNode* RecoverTree(DecodeTree* tree,
                               int* node_num,
                               uint32_t code,
                               int bits,
                               ReadStream* stream) {
  // Pick a node from the node array.
  DecodeNode* this_node = &tree->nodes[*node_num];
  *node_num = *node_num + 1;
  if (UNLIKELY(*node_num) >= kMaxTreeNodes) {
    return NULL;
  }

  // Clear the node.
  this_node->symbol = -1;
  this_node->child_a = NULL;
  this_node->child_b = NULL;

  // Is this a leaf node?
  int is_leaf = ReadBitChecked(stream);
  if (UNLIKELY(stream->read_failed)) {
    return NULL;
  }
  if (is_leaf != 0) {
    // Get symbol from tree description and store in lead node.
    int symbol = (int)(ReadBitsChecked(stream, kSymbolSize));
    if (UNLIKELY(stream->read_failed)) {
      return NULL;
    }

    this_node->symbol = symbol;

    if (bits <= 8) {
      // Fill out the LUT for this symbol, including all permutations of the
      // upper bits.
      uint32_t dups = 256 >> bits;
      for (uint32_t i = 0; i < dups; ++i) {
        DecodeLutEntry* lut_entry = &tree->decode_lut[(i << bits) | code];
        lut_entry->node = NULL;
        lut_entry->bits = hzr_max(bits, 1);  // Special case for single symbol.
        lut_entry->symbol = symbol;
      }
    }

    return this_node;
  }

  if (bits == 8) {
    // This is a branch node with children that have > 8 bits per code. Add a
    // non-terminated entry in the LUT (i.e. one that points into the tree
    // rather than giving a symbol).
    DecodeLutEntry* lut_entry = &tree->decode_lut[code];
    lut_entry->node = this_node;
    lut_entry->bits = 8;
    lut_entry->symbol = 0;
  }

  // Get branch A.
  this_node->child_a = RecoverTree(tree, node_num, code, bits + 1, stream);
  if (UNLIKELY(!this_node->child_a)) {
    return NULL;
  }

  // Get branch B.
  this_node->child_b =
      RecoverTree(tree, node_num, code + (1 << bits), bits + 1, stream);
  if (UNLIKELY(!this_node->child_b)) {
    return NULL;
  }

  return this_node;
}

static hzr_status_t DecodeSingleBlock(ReadStream* stream,
                                      uint8_t* out_ptr,
                                      size_t out_size) {
  // Re-init the bit cache.
  ReInitBitCache(stream);

  // Read the block header.
  size_t encoded_size = (size_t)(ReadBitsChecked(stream, 16) + 1);
  (void)ReadBitsChecked(stream, 32);  // Skip CRC32.
  uint8_t encoding_mode = (uint8_t)ReadBitsChecked(stream, 8);
  if (UNLIKELY(stream->read_failed)) {
    DLOG("Premature end of the input stream.");
    return HZR_FAIL;
  }

  // Plain copy?
  if (encoding_mode == HZR_ENCODING_COPY) {
    if (encoded_size != out_size) {
      DLOG("Encoded / decoded size mismatch (COPY).");
      return HZR_FAIL;
    }
    memcpy(out_ptr, stream->byte_ptr, out_size);
    stream->byte_ptr += out_size;
    return HZR_OK;
  }

  // Fill?
  if (encoding_mode == HZR_ENCODING_FILL) {
    uint8_t fill_value = (uint8_t)ReadBitsChecked(stream, 8);
    if (UNLIKELY(stream->read_failed)) {
      DLOG("Premature end of the input stream.");
      return HZR_FAIL;
    }
    memset(out_ptr, (int)fill_value, out_size);
    return HZR_OK;
  }

  // Check that the encoding mode is valid.
  if (UNLIKELY(encoding_mode != HZR_ENCODING_HUFF_RLE)) {
    DLOG("Invalid encoding mode.");
    return HZR_FAIL;
  }

  // Create a stream that is limited to this block.
  ReadStream block_stream = *stream;
  block_stream.end_ptr = GetBytePtr(&block_stream) + encoded_size;
  if (UNLIKELY(block_stream.end_ptr > stream->end_ptr)) {
    DLOG("Premature end of input stream.");
    return HZR_FAIL;
  }

  // Recover the Huffman tree.
  DecodeTree tree;
  int node_count = 0;
  DecodeNode* tree_root = RecoverTree(&tree, &node_count, 0, 0, &block_stream);
  if (UNLIKELY(tree_root == NULL)) {
    DLOG("Unable to decode the Huffman tree.");
    return HZR_FAIL;
  }

  // Decode the input stream.
  const uint8_t* out_end = out_ptr + out_size;

  // We do the majority of the decoding in a fast, unchecked loop. During this
  // loop, we use a bit cache.
  // Note: The longest supported code + RLE encoding is 32 + 14 bits < 6 bytes.
  // Additionally, the bit cache needs four bytes look-ahead.
  const uint8_t* in_fast_end = block_stream.end_ptr - 10;
  while (block_stream.byte_ptr < in_fast_end) {
    int symbol;

    // Peek 8 bits from the stream and use it to look up a potential symbol in
    // the LUT (codes that are eight bits or shorter are very common, so we have
    // a high hit rate in the LUT).
    const DecodeLutEntry* lut_entry =
        &tree.decode_lut[Peek8Bits(&block_stream)];
    Advance(&block_stream, lut_entry->bits);
    if (LIKELY(lut_entry->node == NULL)) {
      // Fast case: We found the symbol in the LUT.
      symbol = lut_entry->symbol;
    } else {
      // Slow case: Traverse the tree from 8 bits code length until we find a
      // leaf node.
      DecodeNode* node = lut_entry->node;
      while (node->symbol < 0) {
        if (UNLIKELY(block_stream.byte_ptr >= block_stream.end_ptr)) {
          DLOG("Input buffer ended prematurely.");
          return HZR_FAIL;
        }

        // Get next node.
        if (ReadBit(&block_stream)) {
          node = node->child_b;
        } else {
          node = node->child_a;
        }
      }
      symbol = node->symbol;
    }

    // Decode as RLE or plain copy.
    if (LIKELY(symbol <= 255)) {
      // Plain copy.
      if (UNLIKELY(out_ptr >= out_end)) {
        DLOG("Output buffer full.");
        return HZR_FAIL;
      }
      *out_ptr++ = (uint8_t)symbol;
    } else {
      // Symbols >= 256 are RLE tokens.
      size_t zero_count;
      switch (symbol) {
        case kSymTwoZeros: {
          zero_count = 2;
          break;
        }
        case kSymUpTo6Zeros: {
          zero_count = ((size_t)ReadBits(&block_stream, 2)) + 3;
          break;
        }
        case kSymUpTo22Zeros: {
          zero_count = ((size_t)ReadBits(&block_stream, 4)) + 7;
          break;
        }
        case kSymUpTo278Zeros: {
          zero_count = ((size_t)ReadBits(&block_stream, 8)) + 23;
          break;
        }
        case kSymUpTo16662Zeros: {
          zero_count = ((size_t)ReadBits(&block_stream, 14)) + 279;
          break;
        }
        default: {
          // Note: This should never happen -> abort!
          return HZR_FAIL;
        }
      }

      if (UNLIKELY(out_ptr + zero_count > out_end)) {
        DLOG("Output buffer full.");
        return HZR_FAIL;
      }
      memset(out_ptr, 0, zero_count);
      out_ptr += zero_count;
    }
  }

  // ...and we do the tail of the decoding in a slower, checked loop.
  while (out_ptr < out_end) {
    // Traverse the tree until we find a leaf node.
    DecodeNode* node = tree_root;

    // Special case: Only one symbol in the entire tree -> root node is a leaf
    // node.
    if (node->symbol >= 0) {
      AdvanceChecked(&block_stream, 1);

      if (UNLIKELY(block_stream.read_failed)) {
        DLOG("Input buffer ended prematurely.");
        return HZR_FAIL;
      }
    }

    while (node->symbol < 0) {
      // Get next node.
      if (ReadBitChecked(&block_stream)) {
        node = node->child_b;
      } else {
        node = node->child_a;
      }

      if (UNLIKELY(block_stream.read_failed)) {
        DLOG("Input buffer ended prematurely.");
        return HZR_FAIL;
      }
    }
    int symbol = node->symbol;

    // Decode as RLE or plain copy.
    if (LIKELY(symbol <= 255)) {
      // Plain copy.
      *out_ptr++ = (uint8_t)symbol;
    } else {
      // Symbols >= 256 are RLE tokens.
      size_t zero_count;
      switch (symbol) {
        case kSymTwoZeros: {
          zero_count = 2;
          break;
        }
        case kSymUpTo6Zeros: {
          zero_count = ((size_t)ReadBitsChecked(&block_stream, 2)) + 3;
          break;
        }
        case kSymUpTo22Zeros: {
          zero_count = ((size_t)ReadBitsChecked(&block_stream, 4)) + 7;
          break;
        }
        case kSymUpTo278Zeros: {
          zero_count = ((size_t)ReadBitsChecked(&block_stream, 8)) + 23;
          break;
        }
        case kSymUpTo16662Zeros: {
          zero_count = ((size_t)ReadBitsChecked(&block_stream, 14)) + 279;
          break;
        }
        default: {
          // Note: This should never happen -> abort!
          return HZR_FAIL;
        }
      }

      if (UNLIKELY(block_stream.read_failed ||
                   out_ptr + zero_count > out_end)) {
        DLOG("Output buffer full.");
        return HZR_FAIL;
      }
      memset(out_ptr, 0, zero_count);
      out_ptr += zero_count;
    }
  }

  // Commit the stream state.
  CopyReadState(stream, &block_stream);

  // Align the stream pointer to the next byte boundary.
  if (stream->bit_pos != 0) {
    stream->byte_ptr += (stream->bit_pos + 7) >> 3;
    stream->bit_pos = 0;
  }

  return HZR_OK;
}

hzr_status_t hzr_verify(const void* in, size_t in_size, size_t* decoded_size) {
  // Check input parameters.
  if (!in || !decoded_size) {
    DLOG("Invalid input arguments.");
    return HZR_FAIL;
  }

  // Initialize the stream.
  ReadStream stream;
  InitReadStream(&stream, in, in_size);

  // Parse the master header.
  *decoded_size = (size_t)ReadBitsChecked(&stream, 32);
  if (stream.read_failed) {
    DLOG("Could not read the header.");
    return HZR_FAIL;
  }

  // Traverse all the blocks.
  size_t decoded_bytes_left = *decoded_size;
  while (decoded_bytes_left > 0) {
    size_t block_size = hzr_min(decoded_bytes_left, HZR_MAX_BLOCK_SIZE);

    // Parse the block header.
    size_t encoded_size = ((size_t)ReadBitsChecked(&stream, 16)) + 1;
    uint32_t expected_crc32 = ReadBitsChecked(&stream, 32);
    uint8_t encoding_mode = (uint8_t)ReadBitsChecked(&stream, 8);
    if (stream.read_failed) {
      DLOG("Could not read the block header.");
      return HZR_FAIL;
    }
    if (encoding_mode > HZR_ENCODING_LAST) {
      DLOG("Unsupported encoding.");
      return HZR_FAIL;
    }

    // Check the checksum.
    const uint8_t* block_data = GetBytePtr(&stream);
    uint32_t actual_crc32 = _hzr_crc32(block_data, encoded_size);
    if (actual_crc32 != expected_crc32) {
      DLOG("CRC32 check failed.");
      return HZR_FAIL;
    }

    // Skip past the encoded data of this buffer.
    AdvanceBytesChecked(&stream, encoded_size);
    if (stream.read_failed) {
      DLOG("Premature end of input buffer.");
      return HZR_FAIL;
    }

    decoded_bytes_left -= block_size;
  }

  return HZR_OK;
}

hzr_status_t hzr_decode(const void* in,
                        size_t in_size,
                        void* out,
                        size_t out_size) {
  // Check input parameters.
  if (!in || !out) {
    DLOG("Invalid input arguments.");
    return HZR_FAIL;
  }

  // To little input data?
  if (in_size < HZR_HEADER_SIZE) {
    return HZR_FAIL;
  }

  // Read the header.
  ReadStream stream;
  InitReadStream(&stream, in, in_size);
  size_t actual_out_size = (size_t)ReadBitsChecked(&stream, 32);
  if (stream.read_failed) {
    DLOG("Unable to read the header.");
    return HZR_FAIL;
  }
  if (out_size < actual_out_size) {
    DLOG("Insufficient space in the output buffer.");
    return HZR_FAIL;
  }

  // Decompress the input data block by block.
  uint8_t* out_data = (uint8_t*)out;
  size_t output_bytes_left = actual_out_size;
  while (output_bytes_left > 0) {
    size_t this_block_size = hzr_min(output_bytes_left, HZR_MAX_BLOCK_SIZE);
    hzr_status_t status = DecodeSingleBlock(&stream, out_data, this_block_size);
    if (status != HZR_OK) {
      return status;
    }
    out_data += this_block_size;
    output_bytes_left -= this_block_size;
  }

  // TODO: Better check!
  if (UNLIKELY(!AtTheEnd(&stream))) {
    DLOG("Decoder did not reach the end of the input buffer.");
    return HZR_FAIL;
  }

  return HZR_OK;
}
