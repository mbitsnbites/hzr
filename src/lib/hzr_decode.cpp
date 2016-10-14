#include "libhzr.h"

#include <algorithm>
#include <cstdint>

#include "hzr_crc32c.h"
#include "hzr_internal.h"

// A helper for decoding binary data.
struct ReadStream {
  const uint8_t* byte_ptr;
  int bit_pos;
  const uint8_t* end_ptr;
  bool read_failed;
};

// Check if we have reached the end of the buffer.
static bool AtTheEnd(const ReadStream* stream);

// Initialize a bitstream.
static void InitReadStream(ReadStream* stream, const void* buf, int size) {
  stream->byte_ptr = (const uint8_t*)buf;
  stream->bit_pos = 0;
  stream->end_ptr = ((const uint8_t*)buf) + size;
  stream->read_failed = false;
}

// Read one bit from a bitstream.
static int ReadBit(ReadStream* stream) {
  // Extract one bit.
  int x = (*stream->byte_ptr >> stream->bit_pos) & 1;
  stream->bit_pos = (stream->bit_pos + 1) & 7;
  if (stream->bit_pos == 0) {
    ++stream->byte_ptr;
  }

  return x;
}

// Read one bit from a bitstream, with checking.
static int ReadBitChecked(ReadStream* stream) {
  // Check that we don't read past the end.
  if (UNLIKELY(stream->byte_ptr >= stream->end_ptr)) {
    stream->read_failed = true;
    return 0;
  }

  // Ok, read...
  return ReadBit(stream);
}

// Read multiple bits from a bitstream.
static uint32_t ReadBits(ReadStream* stream, int bits) {
  uint32_t x = 0;

  // Get current stream state.
  const uint8_t* buf = stream->byte_ptr;
  int bit = stream->bit_pos;

  // Extract bits.
  // TODO(m): Optimize this!
  int shift = 0;
  while (bits) {
    int bits_to_extract = std::min(bits, 8 - bit);
    bits -= bits_to_extract;

    uint8_t mask = 0xff >> (8 - bits_to_extract);
    x = x | (static_cast<uint32_t>((*buf >> bit) & mask) << shift);
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
static uint32_t ReadBitsChecked(ReadStream* stream, int bits) {
  // Check that we don't read past the end.
  int new_bit_pos = stream->bit_pos + bits;
  const uint8_t* new_byte_ptr = stream->byte_ptr + (new_bit_pos >> 3);
  if (UNLIKELY(new_byte_ptr > stream->end_ptr ||
               (new_byte_ptr == stream->end_ptr && ((new_bit_pos & 7) != 0)))) {
    stream->read_failed = true;
    return 0;
  }

  // Ok, read...
  return ReadBits(stream, bits);
}

// Peek eight bits from a bitstream (read without advancing the pointer).
static uint8_t Peek8Bits(const ReadStream* stream) {
  uint8_t lo = stream->byte_ptr[0], hi = stream->byte_ptr[1];
  return ((hi << 8) | lo) >> stream->bit_pos;
}

// Advance the pointer by N bits.
static void Advance(ReadStream* stream, int N) {
  int new_bit_pos = stream->bit_pos + N;
  stream->bit_pos = new_bit_pos & 7;
  stream->byte_ptr += new_bit_pos >> 3;
}

// Advance N bytes, with checking.
static void AdvanceBytesChecked(ReadStream* stream, int N) {
  const uint8_t* new_byte_ptr = stream->byte_ptr + N;

  // Check that we don't advance past the end.
  if (UNLIKELY(new_byte_ptr > stream->end_ptr ||
               (new_byte_ptr == stream->end_ptr && (stream->bit_pos != 0)))) {
    stream->read_failed = true;
    return;
  }

  // Ok, advance...
  stream->byte_ptr = new_byte_ptr;
}

// Check if we have reached the end of the buffer.
static bool AtTheEnd(const ReadStream* stream) {
  // This is a rought estimate that we have reached the end of the input
  // buffer (not too short, and not too far).
  return (stream->byte_ptr == stream->end_ptr && stream->bit_pos == 0) ||
         (stream->byte_ptr == (stream->end_ptr - 1) && stream->bit_pos > 0);
}

namespace hzr {

namespace {

struct DecodeNode {
  DecodeNode *child_a, *child_b;
  int symbol;
};

struct DecodeLutEntry {
  DecodeNode* node;
  int symbol;
  int bits;
};

struct DecodeTree {
  DecodeNode* Recover(int* node_num,
                      uint32_t code,
                      int bits,
                      ReadStream* stream);

  DecodeNode nodes[kMaxTreeNodes];
  DecodeLutEntry decode_lut[256];
};

// Recursively recover a Huffman tree from a bitstream.
DecodeNode* DecodeTree::Recover(int* node_num,
                                uint32_t code,
                                int bits,
                                ReadStream* stream) {
  // Pick a node from the node array.
  DecodeNode* this_node = &nodes[*node_num];
  *node_num = *node_num + 1;
  if (UNLIKELY(*node_num) >= kMaxTreeNodes) {
    return nullptr;
  }

  // Clear the node.
  this_node->symbol = -1;
  this_node->child_a = nullptr;
  this_node->child_b = nullptr;

  // Is this a leaf node?
  bool is_leaf = ReadBitChecked(stream) != 0;
  if (UNLIKELY(stream->read_failed)) {
    return nullptr;
  }
  if (is_leaf) {
    // Get symbol from tree description and store in lead node.
    int symbol = static_cast<int>(ReadBitsChecked(stream, kSymbolSize));
    if (UNLIKELY(stream->read_failed)) {
      return nullptr;
    }

    this_node->symbol = symbol;

    if (bits <= 8) {
      // Fill out the LUT for this symbol, including all permutations of the
      // upper bits.
      uint32_t dups = 256 >> bits;
      for (uint32_t i = 0; i < dups; ++i) {
        DecodeLutEntry* lut_entry = &decode_lut[(i << bits) | code];
        lut_entry->node = nullptr;
        lut_entry->bits = std::max(bits, 1);  // Special case for single symbol.
        lut_entry->symbol = symbol;
      }
    }

    return this_node;
  }

  if (bits == 8) {
    // This is a branch node with children that have > 8 bits per code. Add a
    // non-terminated entry in the LUT (i.e. one that points into the tree
    // rather than giving a symbol).
    DecodeLutEntry* lut_entry = &decode_lut[code];
    lut_entry->node = this_node;
    lut_entry->bits = 8;
    lut_entry->symbol = 0;
  }

  // Get branch A.
  this_node->child_a = Recover(node_num, code, bits + 1, stream);
  if (UNLIKELY(!this_node->child_a)) {
    return nullptr;
  }

  // Get branch B.
  this_node->child_b = Recover(node_num, code + (1 << bits), bits + 1, stream);
  if (UNLIKELY(!this_node->child_b)) {
    return nullptr;
  }

  return this_node;
}

}  // namespace

}  // namespace hzr

extern "C" hzr_status_t hzr_verify(const void* in,
                                   size_t in_size,
                                   size_t* decoded_size) {
  // Check input parameters.
  if (!in || !decoded_size) {
    DBREAK("Invalid input arguments.");
    return HZR_FAIL;
  }

  // Parse the header.
  ReadStream stream;
  InitReadStream(&stream, in, in_size);
  *decoded_size = static_cast<size_t>(ReadBitsChecked(&stream, 32));
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

extern "C" hzr_status_t hzr_decode(const void* in,
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
  InitReadStream(&stream, in, in_size);
  AdvanceBytesChecked(&stream, HZR_HEADER_SIZE);
  if (stream.read_failed) {
    DBREAK("Unable to skip past the header.");
    return HZR_FAIL;
  }

  // Recover the Huffman tree.
  hzr::DecodeTree tree;
  int node_count = 0;
  hzr::DecodeNode* tree_root = tree.Recover(&node_count, 0, 0, &stream);
  if (tree_root == nullptr) {
    DBREAK("Unable to decode the Huffman tree.");
    return HZR_FAIL;
  }

  // Decode input stream.
  uint8_t* out_ptr = reinterpret_cast<uint8_t*>(out);
  const uint8_t* out_end = out_ptr + out_size;

  // We do the majority of the decoding in a fast, unchecked loop.
  // Note: The longest supported code + RLE encoding is 32 + 14 bits ~= 6 bytes.
  // TODO: THIS IS WRONG! WE NEED TO CHECK THE INPUT BUFFER, NOT THE OUTPUT
  // BUFFER!
  const uint8_t* out_fast_end = out_end - 6;
  while (out_ptr < out_fast_end) {
    int symbol;

    // Peek 8 bits from the stream and use it to look up a potential symbol in
    // the LUT (codes that are eight bits or shorter are very common, so we have
    // a high hit rate in the LUT).
    const auto& lut_entry = tree.decode_lut[Peek8Bits(&stream)];
    Advance(&stream, lut_entry.bits);
    if (LIKELY(lut_entry.node == nullptr)) {
      // Fast case: We found the symbol in the LUT.
      symbol = lut_entry.symbol;
    } else {
      // Slow case: Traverse the tree from 8 bits code length until we find a
      // leaf node.
      hzr::DecodeNode* node = lut_entry.node;
      while (node->symbol < 0) {
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
      *out_ptr++ = static_cast<uint8_t>(symbol);
    } else {
      // Symbols >= 256 are RLE tokens.
      int zero_count;
      switch (symbol) {
        case kSymTwoZeros: {
          zero_count = 2;
          break;
        }
        case kSymUpTo6Zeros: {
          zero_count = static_cast<int>(ReadBits(&stream, 2)) + 3;
          break;
        }
        case kSymUpTo22Zeros: {
          zero_count = static_cast<int>(ReadBits(&stream, 4)) + 7;
          break;
        }
        case kSymUpTo278Zeros: {
          zero_count = static_cast<int>(ReadBits(&stream, 8)) + 23;
          break;
        }
        case kSymUpTo16662Zeros: {
          zero_count = static_cast<int>(ReadBits(&stream, 14)) + 279;
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
      std::fill(out_ptr, out_ptr + zero_count, 0);
      out_ptr += zero_count;
    }
  }

  // ...and we do the tail of the decoding in a slower, checked loop.
  while (out_ptr < out_end) {
    // Traverse the tree until we find a leaf node.
    hzr::DecodeNode* node = tree_root;

    // Special case: Only one symbol in the entire tree -> root node is a leaf
    // node.
    if (node->symbol >= 0) {
      Advance(&stream, 1);
    }

    while (node->symbol < 0) {
      // Get next node.
      if (ReadBitChecked(&stream)) {
        node = node->child_b;
      } else {
        node = node->child_a;
      }

      if (UNLIKELY(stream.read_failed)) {
        DBREAK("Input buffer ended prematurely.");
        return HZR_FAIL;
      }
    }
    int symbol = node->symbol;

    // Decode as RLE or plain copy.
    if (LIKELY(symbol <= 255)) {
      // Plain copy.
      *out_ptr++ = static_cast<uint8_t>(symbol);
    } else {
      // Symbols >= 256 are RLE tokens.
      int zero_count;
      switch (symbol) {
        case kSymTwoZeros: {
          zero_count = 2;
          break;
        }
        case kSymUpTo6Zeros: {
          zero_count = static_cast<int>(ReadBitsChecked(&stream, 2)) + 3;
          break;
        }
        case kSymUpTo22Zeros: {
          zero_count = static_cast<int>(ReadBitsChecked(&stream, 4)) + 7;
          break;
        }
        case kSymUpTo278Zeros: {
          zero_count = static_cast<int>(ReadBitsChecked(&stream, 8)) + 23;
          break;
        }
        case kSymUpTo16662Zeros: {
          zero_count = static_cast<int>(ReadBitsChecked(&stream, 14)) + 279;
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
      std::fill(out_ptr, out_ptr + zero_count, 0);
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
