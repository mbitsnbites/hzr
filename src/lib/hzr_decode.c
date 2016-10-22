#include "libhzr.h"

#include <stdint.h>
#include <string.h>

#include "hzr_crc32c.h"
#include "hzr_internal.h"

// A helper for decoding binary data.
typedef struct {
  const uint8_t* byte_ptr;
  int bit_pos;
  const uint8_t* end_ptr;
  hzr_bool read_failed;
} ReadStream;

// Initialize a bitstream.
static void InitReadStream(ReadStream* stream, const void* buf, int size) {
  stream->byte_ptr = (const uint8_t*)buf;
  stream->bit_pos = 0;
  stream->end_ptr = ((const uint8_t*)buf) + size;
  stream->read_failed = HZR_FALSE;
}

// Read one bit from a bitstream.
FORCE_INLINE static int ReadBit(ReadStream* stream) {
  // Extract one bit.
  int x = ((*stream->byte_ptr) >> stream->bit_pos) & 1;
  int new_bit_pos = stream->bit_pos + 1;
  stream->bit_pos = new_bit_pos & 7;
  stream->byte_ptr += new_bit_pos >> 3;

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
  return ReadBit(stream);
}

// Read multiple bits from a bitstream.
FORCE_INLINE static uint32_t ReadBits(ReadStream* stream, int bits) {
  uint32_t x = 0;

  // Get current stream state.
  const uint8_t* buf = stream->byte_ptr;
  int bit = stream->bit_pos;

  // Extract bits.
  // TODO(m): Optimize this!
  int shift = 0;
  while (bits) {
    int bits_to_extract = hzr_min(bits, 8 - bit);
    bits -= bits_to_extract;

    uint8_t mask = 0xff >> (8 - bits_to_extract);
    x = x | (((uint32_t)((*buf >> bit) & mask)) << shift);
    shift += bits_to_extract;

    bit += bits_to_extract;
    if (bit >= 8) {
      bit -= 8;
      ++buf;
    }
  }

  // Store new stream state.
  stream->bit_pos = bit;
  stream->byte_ptr = buf;

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

  // Ok, read...
  return ReadBits(stream, bits);
}

// Peek eight bits from a bitstream (read without advancing the pointer).
FORCE_INLINE static uint8_t Peek8Bits(const ReadStream* stream) {
  uint32_t lo = stream->byte_ptr[0], hi = stream->byte_ptr[1];
  return (uint8_t)(((hi << 8) | lo) >> stream->bit_pos);
}

// Advance the pointer by N bits.
FORCE_INLINE static void Advance(ReadStream* stream, int N) {
  int new_bit_pos = stream->bit_pos + N;
  stream->bit_pos = new_bit_pos & 7;
  stream->byte_ptr += new_bit_pos >> 3;
}

// Advance N bytes, with checking.
FORCE_INLINE static void AdvanceBytesChecked(ReadStream* stream, int N) {
  const uint8_t* new_byte_ptr = stream->byte_ptr + N;

  // Check that we don't advance past the end.
  if (UNLIKELY(new_byte_ptr > stream->end_ptr ||
               (new_byte_ptr == stream->end_ptr && (stream->bit_pos != 0)))) {
    stream->read_failed = HZR_TRUE;
    return;
  }

  // Ok, advance...
  stream->byte_ptr = new_byte_ptr;
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
  DecodeNode *child_a, *child_b;
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

hzr_status_t hzr_verify(const void* in, size_t in_size, size_t* decoded_size) {
  // Check input parameters.
  if (!in || !decoded_size) {
    DBREAK("Invalid input arguments.");
    return HZR_FAIL;
  }

  // Parse the header.
  ReadStream stream;
  InitReadStream(&stream, in, (int)in_size);  // TODO(m): Preserve precision.
  *decoded_size = (size_t)ReadBitsChecked(&stream, 32);
  uint32_t expected_crc32 = ReadBitsChecked(&stream, 32);
  if (stream.read_failed) {
    DBREAK("Could not read the header.");
    return HZR_FAIL;
  }

  // Check the checksum.
  uint32_t actual_crc32 =
      _hzr_crc32(stream.byte_ptr, in_size - HZR_HEADER_SIZE);
  if (actual_crc32 != expected_crc32) {
    DBREAK("CRC32 check failed.");
    return HZR_FAIL;
  }

  return HZR_OK;
}

hzr_status_t hzr_decode(const void* in,
                        size_t in_size,
                        void* out,
                        size_t out_size) {
  // Check input parameters.
  if (!in || !out) {
    DBREAK("Invalid input arguments.");
    return HZR_FAIL;
  }

  // Nothing to decode?
  if (in_size == HZR_HEADER_SIZE && out_size == 0) {
    return HZR_OK;
  }

  // Skip the header.
  ReadStream stream;
  InitReadStream(&stream, in, (int)in_size);  // TODO(m): Preserve precision.
  AdvanceBytesChecked(&stream, HZR_HEADER_SIZE);
  if (stream.read_failed) {
    DBREAK("Unable to skip past the header.");
    return HZR_FAIL;
  }

  // Recover the Huffman tree.
  DecodeTree tree;
  int node_count = 0;
  DecodeNode* tree_root = RecoverTree(&tree, &node_count, 0, 0, &stream);
  if (tree_root == NULL) {
    DBREAK("Unable to decode the Huffman tree.");
    return HZR_FAIL;
  }

  // Decode input stream.
  uint8_t* out_ptr = (uint8_t*)out;
  const uint8_t* out_end = out_ptr + out_size;

  // We do the majority of the decoding in a fast, unchecked loop.
  // Note: The longest supported code + RLE encoding is 32 + 14 bits < 6 bytes.
  const uint8_t* in_fast_end = stream.end_ptr - 6;
  while (stream.byte_ptr < in_fast_end) {
    int symbol;

    // Peek 8 bits from the stream and use it to look up a potential symbol in
    // the LUT (codes that are eight bits or shorter are very common, so we have
    // a high hit rate in the LUT).
    const DecodeLutEntry* lut_entry = &tree.decode_lut[Peek8Bits(&stream)];
    Advance(&stream, lut_entry->bits);
    if (LIKELY(lut_entry->node == NULL)) {
      // Fast case: We found the symbol in the LUT.
      symbol = lut_entry->symbol;
    } else {
      // Slow case: Traverse the tree from 8 bits code length until we find a
      // leaf node.
      DecodeNode* node = lut_entry->node;
      while (node->symbol < 0) {
        if (UNLIKELY(stream.byte_ptr >= stream.end_ptr)) {
          DBREAK("Input buffer ended prematurely.");
          return HZR_FAIL;
        }

        // Get next node.
        if (ReadBit(&stream)) {
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
        DBREAK("Output buffer full.");
        return HZR_FAIL;
      }
      *out_ptr++ = (uint8_t)symbol;
    } else {
      // Symbols >= 256 are RLE tokens.
      int zero_count;
      switch (symbol) {
        case kSymTwoZeros: {
          zero_count = 2;
          break;
        }
        case kSymUpTo6Zeros: {
          zero_count = ((int)ReadBits(&stream, 2)) + 3;
          break;
        }
        case kSymUpTo22Zeros: {
          zero_count = ((int)ReadBits(&stream, 4)) + 7;
          break;
        }
        case kSymUpTo278Zeros: {
          zero_count = ((int)ReadBits(&stream, 8)) + 23;
          break;
        }
        case kSymUpTo16662Zeros: {
          zero_count = ((int)ReadBits(&stream, 14)) + 279;
          break;
        }
        default: {
          // Note: This should never happen -> abort!
          return HZR_FAIL;
        }
      }

      if (UNLIKELY(out_ptr + zero_count > out_end)) {
        DBREAK("Output buffer full.");
        return HZR_FAIL;
      }
      memset(out_ptr, 0, (size_t)zero_count);
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
      Advance(&stream, 1);
    }

    while (node->symbol < 0) {
      if (UNLIKELY(stream.byte_ptr >= stream.end_ptr)) {
        DBREAK("Input buffer ended prematurely.");
        return HZR_FAIL;
      }

      // Get next node.
      if (ReadBit(&stream)) {
        node = node->child_b;
      } else {
        node = node->child_a;
      }
    }
    int symbol = node->symbol;

    // Decode as RLE or plain copy.
    if (LIKELY(symbol <= 255)) {
      // Plain copy.
      *out_ptr++ = (uint8_t)symbol;
    } else {
      // Symbols >= 256 are RLE tokens.
      int zero_count;
      switch (symbol) {
        case kSymTwoZeros: {
          zero_count = 2;
          break;
        }
        case kSymUpTo6Zeros: {
          zero_count = ((int)ReadBitsChecked(&stream, 2)) + 3;
          break;
        }
        case kSymUpTo22Zeros: {
          zero_count = ((int)ReadBitsChecked(&stream, 4)) + 7;
          break;
        }
        case kSymUpTo278Zeros: {
          zero_count = ((int)ReadBitsChecked(&stream, 8)) + 23;
          break;
        }
        case kSymUpTo16662Zeros: {
          zero_count = ((int)ReadBitsChecked(&stream, 14)) + 279;
          break;
        }
        default: {
          // Note: This should never happen -> abort!
          return HZR_FAIL;
        }
      }

      if (UNLIKELY(stream.read_failed || out_ptr + zero_count > out_end)) {
        DBREAK("Output buffer full.");
        return HZR_FAIL;
      }
      memset(out_ptr, 0, (size_t)zero_count);
      out_ptr += zero_count;
    }
  }

  // TODO: Better check!
  if (UNLIKELY(!AtTheEnd(&stream))) {
    DBREAK("Decoder did not reach the end of the input buffer.");
    return HZR_FAIL;
  }

  return HZR_OK;
}
