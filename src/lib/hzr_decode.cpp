#include "libhzr.h"

#include <algorithm>
#include <cstdint>

#include "hzr_internal.h"

namespace hzr {

namespace {

// A class to help decoding binary data.
class ReadStream {
 public:
  // Initialize a bitstream.
  ReadStream(const uint8_t* buf, int size);

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

ReadStream::ReadStream(const uint8_t* buf, int size)
    : byte_ptr_(buf), bit_pos_(0), end_ptr_(buf + size), read_failed_(false) {}

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
               (new_byte_ptr == end_ptr_ && ((new_bit_pos & 7) > 0)))) {
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
  // TODO(m): Check that we don't read past the end.

  byte_ptr_ += N;
}

bool ReadStream::AtTheEnd() const {
  // This is a rought estimate that we have reached the end of the input
  // buffer (not too short, and not too far).
  return (byte_ptr_ == end_ptr_ && bit_pos_ == 0) ||
         (byte_ptr_ == (end_ptr_ - 1) && bit_pos_ > 0);
}

}  // namespace

}  // namespace hzr

extern "C" hzr_status_t hzr_decode(const void* in,
                                   size_t in_size,
                                   void* out,
                                   size_t out_size) {
  // TODO: Implement me!
  (void)in;
  (void)in_size;
  (void)out;
  (void)out_size;
  return HZR_FAIL;
}
