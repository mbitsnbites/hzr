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
FORCE_INLINE static void ForceFlushBitCache(WriteStream* stream) {
  FlushBitCache(stream);
  if (stream->bit_pos > 0) {
    *stream->byte_ptr =
        (uint8_t)(stream->bit_cache & (0xff >> (8 - stream->bit_pos)));
  }
}

// Write bits to a bitstream.
// NOTE: All unused bits of the input argument x must be zero.
FORCE_INLINE static void WriteBits(WriteStream* stream, uint32_t x, int bits) {
  int bits_to_write = hzr_min(32 - stream->bit_pos, bits);
  stream->bit_cache |= (x << stream->bit_pos);
  stream->bit_pos += bits_to_write;
  bits -= bits_to_write;
  FlushBitCache(stream);

  // In the very unlikely case that we didn't write all the bits in the first
  // pass, we have to do a second pass (the caller has to write *at least*
  // 24 bits at once for this to ever happen).
  if (UNLIKELY(bits > 0)) {
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
    WriteBits(stream, (uint32_t)node->symbol, kSymbolSize);

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
  hzr_bool has_zeros = 0;
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

static hzr_status_t PlainCopy(const void* in,
                              size_t in_size,
                              void* out,
                              size_t out_size,
                              size_t* encoded_size) {
  // Check that the output buffer is large enough.
  if (UNLIKELY(out_size < (in_size + HZR_HEADER_SIZE))) {
    DLOG("Output buffer too small for a plain copy.");
    return HZR_FAIL;
  }

  // Copy the input buffer to the output buffer.
  memcpy(((uint8_t*)out) + HZR_HEADER_SIZE, in, in_size);

  // Calculate the CRC for the buffer.
  uint32_t crc32 = _hzr_crc32(in, in_size);

  // Write the header.
  WriteStream hdr_stream;
  InitWriteStream(&hdr_stream, out, out_size);
  WriteBits(&hdr_stream, (uint32_t)in_size, 32);
  WriteBits(&hdr_stream, crc32, 32);
  WriteBits(&hdr_stream, HZR_ENCODING_COPY, 8);

  // Calculate the encoded size.
  *encoded_size = in_size + HZR_HEADER_SIZE;

  return HZR_OK;
}

static hzr_status_t EncodeFill(const void* in,
                               size_t in_size,
                               void* out,
                               size_t out_size,
                               size_t* encoded_size) {
  // Check that the output buffer is large enough.
  if (UNLIKELY(out_size < (HZR_HEADER_SIZE + 1))) {
    DLOG("Output buffer too small for fill encoding.");
    return HZR_FAIL;
  }

  // Calculate the CRC for the buffer.
  uint32_t crc32 = _hzr_crc32(in, 1);

  // Write the header.
  WriteStream hdr_stream;
  InitWriteStream(&hdr_stream, out, out_size);
  WriteBits(&hdr_stream, (uint32_t)in_size, 32);
  WriteBits(&hdr_stream, crc32, 32);
  WriteBits(&hdr_stream, HZR_ENCODING_FILL, 8);

  // Write the fill code.
  WriteBits(&hdr_stream, (uint32_t)(*(uint8_t*)in), 8);

  // Calculate the encoded size.
  *encoded_size = HZR_HEADER_SIZE + 1;

  return HZR_OK;
}

size_t hzr_max_compressed_size(size_t uncompressed_size) {
  return uncompressed_size + HZR_HEADER_SIZE;
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

  // Check that there is enought space in the output buffer for the header.
  if (UNLIKELY(out_size < HZR_HEADER_SIZE)) {
    DLOG("The output buffer is too small.");
    return HZR_FAIL;
  }

  const uint8_t* in_data = (const uint8_t*)in;

  // Initialize the output stream.
  WriteStream stream;
  uint8_t* encoded_start = ((uint8_t*)out) + HZR_HEADER_SIZE;
  InitWriteStream(&stream, encoded_start, out_size - HZR_HEADER_SIZE);

  // Calculate the histogram for input data.
  SymbolInfo symbols[kNumSymbols];
  Histogram(in_data, symbols, in_size);

  // Check if we have a single symbol.
  if (OnlySingleCode(symbols)) {
    return EncodeFill(in, in_size, out, out_size, encoded_size);
  }

  // Build the Huffman tree, and write it to the output stream.
  MakeTree(symbols, &stream);
  if (UNLIKELY(stream.write_failed)) {
    return PlainCopy(in, in_size, out, out_size, encoded_size);
  }

  // Encode the input stream.
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
        WriteBits(&stream, symbols[0].code, symbols[0].bits);
      } else if (zeros == 2) {
        WriteBits(&stream, symbols[kSymTwoZeros].code,
                  symbols[kSymTwoZeros].bits);
      } else if (zeros <= 6) {
        uint32_t count = (uint32_t)(zeros - 3);
        WriteBits(&stream, symbols[kSymUpTo6Zeros].code,
                  symbols[kSymUpTo6Zeros].bits);
        WriteBits(&stream, count, 2);
      } else if (zeros <= 22) {
        uint32_t count = (uint32_t)(zeros - 7);
        WriteBits(&stream, symbols[kSymUpTo22Zeros].code,
                  symbols[kSymUpTo22Zeros].bits);
        WriteBits(&stream, count, 4);
      } else if (zeros <= 278) {
        uint32_t count = (uint32_t)(zeros - 23);
        WriteBits(&stream, symbols[kSymUpTo278Zeros].code,
                  symbols[kSymUpTo278Zeros].bits);
        WriteBits(&stream, count, 8);
      } else {
        uint32_t count = (uint32_t)(zeros - 279);
        WriteBits(&stream, symbols[kSymUpTo16662Zeros].code,
                  symbols[kSymUpTo16662Zeros].bits);
        WriteBits(&stream, count, 14);
      }
      k += zeros;
    } else {
      WriteBits(&stream, symbols[symbol].code, symbols[symbol].bits);
      k++;
    }

    if (UNLIKELY(stream.write_failed)) {
      return PlainCopy(in, in_size, out, out_size, encoded_size);
    }
  }

  // Write final bits to the stream.
  ForceFlushBitCache(&stream);
  if (UNLIKELY(stream.write_failed)) {
    return PlainCopy(in, in_size, out, out_size, encoded_size);
  }

  // Calculate the size of the encoded output data.
  *encoded_size = (size_t)(stream.byte_ptr - (uint8_t*)out);
  if (stream.bit_pos > 0) {
    *encoded_size = *encoded_size + 1;
  }

  // Calculate the CRC for the compressed buffer.
  uint32_t crc32 = _hzr_crc32(encoded_start, *encoded_size - HZR_HEADER_SIZE);

  // Write the header.
  {
    WriteStream hdr_stream;
    InitWriteStream(&hdr_stream, out, out_size);
    WriteBits(&hdr_stream, (uint32_t)in_size, 32);
    WriteBits(&hdr_stream, crc32, 32);
    WriteBits(&hdr_stream, HZR_ENCODING_HUFF_RLE, 8);
  }

  return HZR_OK;
}
