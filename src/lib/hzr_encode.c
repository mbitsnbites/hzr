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
  uint8_t* byte_ptr;
  uint8_t* end_ptr;
  int bit_pos;
  uint32_t bit_cache;
  hzr_bool write_failed;  // TODO(m): Implement buffer overflow checking!
} WriteStream;

// Initialize a bitstream.
static void InitWriteStream(WriteStream* stream, void* buf, size_t size) {
  stream->byte_ptr = (uint8_t*)buf;
  stream->end_ptr = ((uint8_t*)buf) + size;
  stream->bit_pos = 0;
  stream->bit_cache = 0U;
  stream->write_failed = HZR_FALSE;
}

// Copy the write state from one stream to another without altering the end
// pointer.
static void CopyWriteState(WriteStream* stream, const WriteStream* src_stream) {
  stream->byte_ptr = src_stream->byte_ptr;
  stream->bit_pos = src_stream->bit_pos;
  stream->bit_cache = src_stream->bit_cache;
  stream->write_failed = src_stream->write_failed;
}

FORCE_INLINE static uint8_t* GetBytePtr(WriteStream* stream) {
  return stream->byte_ptr + (stream->bit_pos >> 3);
}

// Write the bit cache to the write stream if necessary.
FORCE_INLINE static void FlushBitCache(WriteStream* stream) {
  while (stream->bit_pos >= 8) {
    if (UNLIKELY(stream->byte_ptr >= stream->end_ptr)) {
      stream->write_failed = HZR_TRUE;
      return;
    }
    *stream->byte_ptr = (uint8_t)stream->bit_cache;
    stream->bit_cache >>= 8;
    stream->bit_pos -= 8;
    stream->byte_ptr++;
  }
}

// Write the bit cache to the write stream - includig incomplete words.
static void ForceFlushBitCache(WriteStream* stream) {
  FlushBitCache(stream);
  if (stream->bit_pos > 0) {
    if (UNLIKELY(stream->byte_ptr >= stream->end_ptr)) {
      stream->write_failed = HZR_TRUE;
      return;
    }
    *stream->byte_ptr =
        (uint8_t)(stream->bit_cache & (0xff >> (8 - stream->bit_pos)));
    stream->bit_cache = 0U;
    stream->bit_pos = 0;
    stream->byte_ptr++;
  }
}

// Write bits to a bitstream.
// NOTE: All unused bits of the input argument x must be zero.
FORCE_INLINE static void WriteBits(WriteStream* stream, uint32_t x, int bits) {
  ASSERT(bits <= 32);
  ASSERT(stream->bit_pos < 32);

  int bits_to_write = hzr_min(32 - stream->bit_pos, bits);
  stream->bit_cache |= (x << stream->bit_pos);
  stream->bit_pos += bits_to_write;
  bits -= bits_to_write;
  FlushBitCache(stream);

  // In the very unlikely case that we didn't write all the bits in the first
  // pass, we have to do a second pass (the caller has to write *at least*
  // 24 bits at once for this to ever happen).
  if (UNLIKELY(bits > 0 && !stream->write_failed)) {
    x >>= bits_to_write;
    stream->bit_cache |= (x << stream->bit_pos);
    stream->bit_pos += bits;
    FlushBitCache(stream);
  }
}

// Used by the encoder for building the optimal Huffman tree.
typedef struct {
  Symbol symbol;  // TODO(m): Is this needed?!
                  // All SymbolInfos seem to be indexed by the symbol no.
  int count;
  uint32_t code;
  int bits;
} SymbolInfo;

typedef struct EncodeNode_struct EncodeNode;
struct EncodeNode_struct {
  EncodeNode* child_a;
  EncodeNode* child_b;
  int count;
  int symbol;
};

// Calculate histogram for a block of data.
static void Histogram(const uint8_t* in, SymbolInfo* symbols, size_t in_size) {
  // Clear/init histogram.
  for (int k = 0; k < kNumSymbols; ++k) {
    symbols[k].symbol = (Symbol)k;
    symbols[k].count = 0;
    symbols[k].code = 0;
    symbols[k].bits = 0;
  }

  // Build the histogram for this block.
  for (size_t k = 0; k < in_size;) {
    Symbol symbol = (Symbol)in[k];

    // Possible RLE?
    if (symbol == 0) {
      size_t zeros;
      for (zeros = 1U; zeros < 16662U && (k + zeros) < in_size; ++zeros) {
        if (in[k + zeros] != 0) {
          break;
        }
      }
      if (zeros == 1U) {
        symbols[0].count++;
      } else if (zeros == 2U) {
        symbols[kSymTwoZeros].count++;
      } else if (zeros <= 6U) {
        symbols[kSymUpTo6Zeros].count++;
      } else if (zeros <= 22U) {
        symbols[kSymUpTo22Zeros].count++;
      } else if (zeros <= 278U) {
        symbols[kSymUpTo278Zeros].count++;
      } else {
        symbols[kSymUpTo16662Zeros].count++;
      }
      k += zeros;
    } else {
      symbols[symbol].count++;
      k++;
    }
  }
}

// Store a Huffman tree in the output stream and in a look-up-table (a symbol
// array).
static void StoreTree(EncodeNode* node,
                      SymbolInfo* symbols,
                      WriteStream* stream,
                      uint32_t code,
                      int bits) {
  // Is this a leaf node?
  if (node->symbol >= 0) {
    // Append symbol to tree description.
    WriteBits(stream, 1, 1);
    if (UNLIKELY(stream->write_failed)) {
      return;
    }
    WriteBits(stream, (uint32_t)node->symbol, kSymbolSize);
    if (UNLIKELY(stream->write_failed)) {
      return;
    }

    // Find symbol index.
    int sym_idx;
    for (sym_idx = 0; sym_idx < kNumSymbols; ++sym_idx) {
      if (symbols[sym_idx].symbol == (Symbol)node->symbol) {
        break;
      }
    }

    // Store code info in symbol array.
    symbols[sym_idx].code = code;
    symbols[sym_idx].bits = bits;
    return;
  }

  // This was not a leaf node.
  WriteBits(stream, 0, 1);
  if (UNLIKELY(stream->write_failed)) {
    return;
  }

  // Branch A.
  StoreTree(node->child_a, symbols, stream, code, bits + 1);

  // Branch B.
  StoreTree(node->child_b, symbols, stream, code + (1 << bits), bits + 1);
}

// Generate a Huffman tree.
static void MakeTree(SymbolInfo* sym, WriteStream* stream) {
  // Initialize all leaf nodes.
  EncodeNode nodes[kMaxTreeNodes];
  int num_symbols = 0;
  for (int k = 0; k < kNumSymbols; ++k) {
    if (sym[k].count > 0) {
      nodes[num_symbols].symbol = (int)sym[k].symbol;
      nodes[num_symbols].count = sym[k].count;
      nodes[num_symbols].child_a = NULL;
      nodes[num_symbols].child_b = NULL;
      ++num_symbols;
    }
  }

  // Special case: No symbols at all - don't store anything in the output
  // stream.
  if (num_symbols == 0) {
    return;
  }

  // Build tree by joining the lightest nodes until there is only one node left
  // (the root node).
  EncodeNode* root = NULL;
  int nodes_left = num_symbols;
  int next_idx = num_symbols;
  while (nodes_left > 1) {
    // Find the two lightest nodes.
    EncodeNode* node_1 = NULL;
    EncodeNode* node_2 = NULL;
    for (int k = 0; k < next_idx; ++k) {
      if (nodes[k].count > 0) {
        if (!node_1 || (nodes[k].count <= node_1->count)) {
          node_2 = node_1;
          node_1 = &nodes[k];
        } else if (!node_2 || (nodes[k].count <= node_2->count)) {
          node_2 = &nodes[k];
        }
      }
    }

    // Join the two nodes into a new parent node.
    root = &nodes[next_idx];
    root->child_a = node_1;
    root->child_b = node_2;
    root->count = node_1->count + node_2->count;
    root->symbol = -1;
    node_1->count = 0;
    node_2->count = 0;
    ++next_idx;
    --nodes_left;
  }

  // Store the tree in the output stream, and in the sym[] array (the latter is
  // used as a look-up-table for faster encoding).
  if (root) {
    StoreTree(root, sym, stream, 0, 0);
  } else {
    // Special case: only one symbol => no binary tree.
    root = &nodes[0];
    StoreTree(root, sym, stream, 0, 1);
  }
}

static hzr_bool OnlySingleCode(const SymbolInfo* const symbols) {
  int used_codes = 0;
  int has_zeros = 0;
  int num_nonzero_codes = 0;
  for (int k = 0; k < kNumSymbols; ++k) {
    if (symbols[k].count > 0) {
      Symbol x = symbols[k].symbol;
      if ((x == 0) || (x >= 256)) {
        has_zeros = 1;
      } else {
        ++num_nonzero_codes;
      }
      used_codes = num_nonzero_codes + has_zeros;
      if (LIKELY(used_codes > 1)) {
        break;
      }
    }
  }

  return (used_codes == 1) ? HZR_TRUE : HZR_FALSE;
}

static hzr_status_t PlainCopy(const uint8_t* in,
                              size_t in_size,
                              WriteStream* stream,
                              size_t* encoded_size) {
  // Check that the output buffer is large enough.
  if (UNLIKELY((GetBytePtr(stream) + HZR_BLOCK_HEADER_SIZE + in_size) >
               stream->end_ptr)) {
    DLOG("Output buffer too small for a plain copy.");
    return HZR_FAIL;
  }

  // Calculate the CRC for the buffer.
  uint32_t crc32 = _hzr_crc32(in, in_size);

  // Write the block header.
  WriteBits(stream, (uint32_t)(in_size - 1), 16);
  WriteBits(stream, crc32, 32);
  WriteBits(stream, HZR_ENCODING_COPY, 8);
  ForceFlushBitCache(stream);

  // Copy the input buffer to the output buffer.
  memcpy(GetBytePtr(stream), in, in_size);

  // Advance the stream.
  // Note: It is safe to just increase the byte pointer here, since the stream
  // is byte aligned.
  stream->byte_ptr += in_size;

  // Calculate the encoded size.
  *encoded_size = in_size + HZR_BLOCK_HEADER_SIZE;

  return HZR_OK;
}

static hzr_status_t EncodeFill(const uint8_t* in,
                               WriteStream* stream,
                               size_t* encoded_size) {
  // Check that the output buffer is large enough.
  if (UNLIKELY((GetBytePtr(stream) + HZR_BLOCK_HEADER_SIZE + 1) >
               stream->end_ptr)) {
    DLOG("Output buffer too small for fill encoding.");
    return HZR_FAIL;
  }

  // Calculate the CRC for the buffer.
  uint32_t crc32 = _hzr_crc32(in, 1);

  // Write the block header.
  WriteBits(stream, 0U, 16);
  WriteBits(stream, crc32, 32);
  WriteBits(stream, HZR_ENCODING_FILL, 8);

  // Write the fill code.
  WriteBits(stream, (uint32_t)(*in), 8);
  ForceFlushBitCache(stream);

  // Calculate the encoded size.
  *encoded_size = HZR_BLOCK_HEADER_SIZE + 1;

  return HZR_OK;
}

static hzr_status_t EncodeSingleBlock(WriteStream* stream,
                                      const uint8_t* in,
                                      size_t in_size,
                                      size_t* encoded_size) {
  ASSERT((stream->bit_pos & 7) == 0);

  // Create a stream that is limited to this block (this is required to detect
  // block buffer overruns).
  WriteStream block_stream = *stream;
  block_stream.end_ptr =
      GetBytePtr(&block_stream) + HZR_BLOCK_HEADER_SIZE + in_size;
  if (block_stream.end_ptr > stream->end_ptr) {
    block_stream.end_ptr = stream->end_ptr;
  }

  // Zero out the block header (will be filled out later).
  if (UNLIKELY((GetBytePtr(&block_stream) + HZR_BLOCK_HEADER_SIZE) >
               block_stream.end_ptr)) {
    DLOG("Block buffer is too small for holding the block header.");
    return HZR_FAIL;
  }
  WriteBits(&block_stream, 0U, 16);
  WriteBits(&block_stream, 0U, 32);
  WriteBits(&block_stream, 0U, 8);

  // Calculate the histogram for input data.
  SymbolInfo symbols[kNumSymbols];
  Histogram(in, symbols, in_size);

  // Check if we have a single symbol.
  if (OnlySingleCode(symbols)) {
    return EncodeFill(in, stream, encoded_size);
  }

  // Build the Huffman tree, and write it to the output stream.
  MakeTree(symbols, &block_stream);
  if (UNLIKELY(block_stream.write_failed)) {
    return PlainCopy(in, in_size, stream, encoded_size);
  }

  // Encode the input stream.
  const uint8_t* in_data = in;
  for (size_t k = 0; k < in_size;) {
    uint8_t symbol = in_data[k];

    // Possible RLE?
    if (symbol == 0) {
      size_t zeros;
      for (zeros = 1U; zeros < 16662U && (k + zeros) < in_size; ++zeros) {
        if (in_data[k + zeros] != 0) {
          break;
        }
      }
      if (zeros == 1) {
        WriteBits(&block_stream, symbols[0].code, symbols[0].bits);
      } else if (zeros == 2) {
        WriteBits(&block_stream, symbols[kSymTwoZeros].code,
                  symbols[kSymTwoZeros].bits);
      } else if (zeros <= 6) {
        uint32_t count = (uint32_t)(zeros - 3);
        WriteBits(&block_stream, symbols[kSymUpTo6Zeros].code,
                  symbols[kSymUpTo6Zeros].bits);
        WriteBits(&block_stream, count, 2);
      } else if (zeros <= 22) {
        uint32_t count = (uint32_t)(zeros - 7);
        WriteBits(&block_stream, symbols[kSymUpTo22Zeros].code,
                  symbols[kSymUpTo22Zeros].bits);
        WriteBits(&block_stream, count, 4);
      } else if (zeros <= 278) {
        uint32_t count = (uint32_t)(zeros - 23);
        WriteBits(&block_stream, symbols[kSymUpTo278Zeros].code,
                  symbols[kSymUpTo278Zeros].bits);
        WriteBits(&block_stream, count, 8);
      } else {
        uint32_t count = (uint32_t)(zeros - 279);
        WriteBits(&block_stream, symbols[kSymUpTo16662Zeros].code,
                  symbols[kSymUpTo16662Zeros].bits);
        WriteBits(&block_stream, count, 14);
      }
      k += zeros;
    } else {
      WriteBits(&block_stream, symbols[symbol].code, symbols[symbol].bits);
      k++;
    }

    if (UNLIKELY(block_stream.write_failed)) {
      return PlainCopy(in, in_size, stream, encoded_size);
    }
  }

  // Write final bits to the stream.
  ForceFlushBitCache(&block_stream);

  // Make sure that the compressed buffer fit into this block.
  size_t encoded_size_wo_hdr =
      (size_t)(GetBytePtr(&block_stream) - GetBytePtr(stream)) -
      HZR_BLOCK_HEADER_SIZE;
  if (UNLIKELY(block_stream.write_failed ||
               (encoded_size_wo_hdr >= HZR_MAX_BLOCK_SIZE))) {
    return PlainCopy(in, in_size, stream, encoded_size);
  }

  // Return the size of the encoded output data.
  *encoded_size = encoded_size_wo_hdr + HZR_BLOCK_HEADER_SIZE;

  // Calculate the CRC for the compressed buffer.
  uint8_t* encoded_start = GetBytePtr(stream) + HZR_BLOCK_HEADER_SIZE;
  uint32_t crc32 = _hzr_crc32(encoded_start, encoded_size_wo_hdr);

  // Write the block header.
  WriteBits(stream, (uint32_t)(encoded_size_wo_hdr - 1), 16);
  WriteBits(stream, crc32, 32);
  WriteBits(stream, HZR_ENCODING_HUFF_RLE, 8);

  // Commit the stream state.
  CopyWriteState(stream, &block_stream);

  return HZR_OK;
}

size_t hzr_max_compressed_size(size_t uncompressed_size) {
  size_t data_size = 0;
  if (uncompressed_size > 0) {
    size_t num_blocks =
        (uncompressed_size + HZR_MAX_BLOCK_SIZE - 1) / HZR_MAX_BLOCK_SIZE;
    data_size = (num_blocks * HZR_BLOCK_HEADER_SIZE) + uncompressed_size;
  }
  return HZR_HEADER_SIZE + data_size;
}

hzr_status_t hzr_encode(const void* in,
                        size_t in_size,
                        void* out,
                        size_t out_size,
                        size_t* encoded_size) {
  // Check input arguments.
  if (UNLIKELY(!in || !out || !encoded_size)) {
    DLOG("Invalid input arguments.");
    return HZR_FAIL;
  }

  // Check that there is enough space in the output buffer for the header.
  if (UNLIKELY(out_size < HZR_HEADER_SIZE)) {
    DLOG("The output buffer is too small.");
    return HZR_FAIL;
  }

  // Initialize the output stream.
  WriteStream stream;
  InitWriteStream(&stream, out, out_size);

  // Write the master header.
  WriteBits(&stream, (uint32_t)in_size, 32);
  ForceFlushBitCache(&stream);
  size_t total_encoded_size = HZR_HEADER_SIZE;

  // Compress the input data block by block.
  const uint8_t* in_data = (const uint8_t*)in;
  size_t input_bytes_left = in_size;
  while (input_bytes_left > 0) {
    size_t this_block_size = hzr_min(input_bytes_left, HZR_MAX_BLOCK_SIZE);
    size_t this_encoded_size = 0;
    hzr_status_t status = EncodeSingleBlock(&stream, in_data, this_block_size,
                                            &this_encoded_size);
    if (status != HZR_OK) {
      return status;
    }
    total_encoded_size += this_encoded_size;
    in_data += this_block_size;
    input_bytes_left -= this_block_size;
  }

  // Compression succeeded.
  *encoded_size = total_encoded_size;
  return HZR_OK;
}
