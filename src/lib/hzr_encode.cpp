#include "libhzr.h"

#include <cstdint>

#include "hzr_internal.h"

namespace hzr {

namespace {}  // namespace

}  // namespace hzr

extern "C" size_t hzr_max_compressed_size(size_t uncompressed_size) {
  // TODO: Implement me!
  return uncompressed_size * 2 + 500;
}

extern "C" hzr_status_t hzr_encode(const void* in,
                                   size_t in_size,
                                   void* out,
                                   size_t out_size,
                                   size_t* encoded_size) {
  // TODO: Implement me!
  (void)in;
  (void)in_size;
  (void)out;
  (void)out_size;
  (void)encoded_size;
  return HZR_FAIL;
}
