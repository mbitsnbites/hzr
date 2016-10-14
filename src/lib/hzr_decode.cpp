#include "libhzr.h"

#include <algorithm>
#include <cstdint>

#include "hzr_crc32c.h"
#include "hzr_internal.h"

namespace hzr {

namespace {

// A class to help decoding binary data.
class ReadStream {
 public:
  // Initialize a bitstream.
  ReadStream(const void* buf, int size);

  // Copy constructor.
  ReadStream(const ReadStream& other);

  // Read one bit from a bitstream.
  int ReadBit();

  // Read one bit from a bitstream, with checking.
  int ReadBitChecked();

  // Read bits from a bitstream.
  uint32_t ReadBits(int bits);

  // Read bits from a bitstream, with checking.
  uint32_t ReadBitsChecked(int bits);

  // Peek eight bits from a bitstream (read without advancing the pointer).
  uint8_t Peek8Bits() const;

  // Read 16 bits from a bitstream, byte aligned.
  uint32_t Read16BitsAligned();

  // Align the stream to a byte boundary (do nothing if already aligned).
  void AlignToByte();

  // Advance the pointer by N bits.
  void Advance(int N);

  // Advance N bytes.
  void AdvanceBytes(int N);

  // Advance N bytes, with checking.
  void AdvanceBytesChecked(int N);

  // Check if we have reached the end of the buffer.
  bool AtTheEnd() const;

  const uint8_t* byte_ptr() const { return byte_ptr_; }

  // Check if any of the Read*Checked() methods failed.
  bool read_failed() const { return read_failed_; }

 private:
  const uint8_t* byte_ptr_;
  int bit_pos_;
  const uint8_t* end_ptr_;
  bool read_failed_;
};

ReadStream::ReadStream(const void* buf, int size)
    : byte_ptr_(reinterpret_cast<const uint8_t*>(buf)),
      bit_pos_(0),
      end_ptr_(reinterpret_cast<const uint8_t*>(buf) + size),
      read_failed_(false) {}

ReadStream::ReadStream(const ReadStream& other)
    : byte_ptr_(other.byte_ptr_),
      bit_pos_(other.bit_pos_),
      end_ptr_(other.end_ptr_),
      read_failed_(other.read_failed_) {}

int ReadStream::ReadBit() {
  // Extract one bit.
  int x = (*byte_ptr_ >> bit_pos_) & 1;
  bit_pos_ = (bit_pos_ + 1) & 7;
  if (!bit_pos_) {
    ++byte_ptr_;
  }

  return x;
}

int ReadStream::ReadBitChecked() {
  // Check that we don't read past the end.
  if (UNLIKELY(byte_ptr_ >= end_ptr_)) {
    read_failed_ = true;
    return 0;
  }

  // Ok, read...
  return ReadBit();
}

uint32_t ReadStream::ReadBits(int bits) {
  uint32_t x = 0;

  // Get current stream state.
  const uint8_t* buf = byte_ptr_;
  int bit = bit_pos_;

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
  bit_pos_ = bit;
  byte_ptr_ = buf;

  return x;
}

uint32_t ReadStream::ReadBitsChecked(int bits) {
  // Check that we don't read past the end.
  int new_bit_pos = bit_pos_ + bits;
  const uint8_t* new_byte_ptr = byte_ptr_ + (new_bit_pos >> 3);
  if (UNLIKELY(new_byte_ptr > end_ptr_ ||
               (new_byte_ptr == end_ptr_ && ((new_bit_pos & 7) != 0)))) {
    read_failed_ = true;
    return 0;
  }

  // Ok, read...
  return ReadBits(bits);
}

uint8_t ReadStream::Peek8Bits() const {
  uint8_t lo = byte_ptr_[0], hi = byte_ptr_[1];
  return ((hi << 8) | lo) >> bit_pos_;
}

uint32_t ReadStream::Read16BitsAligned() {
  // TODO(m): Check that we don't read past the end.

  AlignToByte();
  uint32_t lo = byte_ptr_[0], hi = byte_ptr_[1];
  byte_ptr_ += 2;
  return (hi << 8) | lo;
}

void ReadStream::AlignToByte() {
  if (LIKELY(bit_pos_)) {
    bit_pos_ = 0;
    ++byte_ptr_;
  }
}
void ReadStream::Advance(int N) {
  int new_bit_pos = bit_pos_ + N;
  bit_pos_ = new_bit_pos & 7;
  byte_ptr_ += new_bit_pos >> 3;
}

void ReadStream::AdvanceBytes(int N) {
  byte_ptr_ += N;
}

void ReadStream::AdvanceBytesChecked(int N) {
  const uint8_t* new_byte_ptr = byte_ptr_ + N;

  // Check that we don't advance past the end.
  if (UNLIKELY(new_byte_ptr > end_ptr_ ||
               (new_byte_ptr == end_ptr_ && (bit_pos_ != 0)))) {
    read_failed_ = true;
    return;
  }

  // Ok, advance...
  byte_ptr_ = new_byte_ptr;
}

bool ReadStream::AtTheEnd() const {
  // This is a rought estimate that we have reached the end of the input
  // buffer (not too short, and not too far).
  return (byte_ptr_ == end_ptr_ && bit_pos_ == 0) ||
         (byte_ptr_ == (end_ptr_ - 1) && bit_pos_ > 0);
}

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
                      ReadStream& stream);

  DecodeNode nodes[kMaxTreeNodes];
  DecodeLutEntry decode_lut[256];
};

// Recursively recover a Huffman tree from a bitstream.
DecodeNode* DecodeTree::Recover(int* node_num,
                                uint32_t code,
                                int bits,
                                ReadStream& stream) {
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
  bool is_leaf = stream.ReadBitChecked() != 0;
  if (UNLIKELY(stream.read_failed())) {
    return nullptr;
  }
  if (is_leaf) {
    // Get symbol from tree description and store in lead node.
    int symbol = static_cast<int>(stream.ReadBitsChecked(kSymbolSize));
    if (UNLIKELY(stream.read_failed())) {
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
  hzr::ReadStream stream(in, in_size);
  *decoded_size = static_cast<size_t>(stream.ReadBitsChecked(32));
  uint32_t expected_crc32 = stream.ReadBitsChecked(32);
  if (stream.read_failed()) {
    DBREAK("Could not read the header.");
    return HZR_FAIL;
  }

  // Check the checksum.
  uint32_t actual_crc32 =
      _hzr_crc32(stream.byte_ptr(), in_size - HZR_HEADER_SIZE);
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
  hzr::ReadStream stream(in, in_size);
  stream.AdvanceBytesChecked(HZR_HEADER_SIZE);
  if (stream.read_failed()) {
    DBREAK("Unable to skip past the header.");
    return HZR_FAIL;
  }

  // Recover the Huffman tree.
  hzr::DecodeTree tree;
  int node_count = 0;
  hzr::DecodeNode* tree_root = tree.Recover(&node_count, 0, 0, stream);
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
    const auto& lut_entry = tree.decode_lut[stream.Peek8Bits()];
    stream.Advance(lut_entry.bits);
    if (LIKELY(lut_entry.node == nullptr)) {
      // Fast case: We found the symbol in the LUT.
      symbol = lut_entry.symbol;
    } else {
      // Slow case: Traverse the tree from 8 bits code length until we find a
      // leaf node.
      hzr::DecodeNode* node = lut_entry.node;
      while (node->symbol < 0) {
        // Get next node.
        if (stream.ReadBit()) {
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
          zero_count = static_cast<int>(stream.ReadBits(2)) + 3;
          break;
        }
        case kSymUpTo22Zeros: {
          zero_count = static_cast<int>(stream.ReadBits(4)) + 7;
          break;
        }
        case kSymUpTo278Zeros: {
          zero_count = static_cast<int>(stream.ReadBits(8)) + 23;
          break;
        }
        case kSymUpTo16662Zeros: {
          zero_count = static_cast<int>(stream.ReadBits(14)) + 279;
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
      stream.Advance(1);
    }

    while (node->symbol < 0) {
      // Get next node.
      if (stream.ReadBitChecked()) {
        node = node->child_b;
      } else {
        node = node->child_a;
      }

      if (UNLIKELY(stream.read_failed())) {
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
          zero_count = static_cast<int>(stream.ReadBitsChecked(2)) + 3;
          break;
        }
        case kSymUpTo22Zeros: {
          zero_count = static_cast<int>(stream.ReadBitsChecked(4)) + 7;
          break;
        }
        case kSymUpTo278Zeros: {
          zero_count = static_cast<int>(stream.ReadBitsChecked(8)) + 23;
          break;
        }
        case kSymUpTo16662Zeros: {
          zero_count = static_cast<int>(stream.ReadBitsChecked(14)) + 279;
          break;
        }
        default: {
          // Note: This should never happen -> abort!
          return HZR_FAIL;
        }
      }

      if (UNLIKELY(stream.read_failed() || out_ptr + zero_count > out_end)) {
        DBREAK("Output buffer full.");
        return HZR_FAIL;
      }
      std::fill(out_ptr, out_ptr + zero_count, 0);
      out_ptr += zero_count;
    }
  }

  // TODO: Better check!
  if (UNLIKELY(!stream.AtTheEnd())) {
    DBREAK("Decoder did not reach the end of the input buffer.");
    return HZR_FAIL;
  }

  return HZR_OK;
}
