#include "headers/lfs.h"

uint32_t align_up(uint32_t x, uint32_t a) {
  return ((x + a - 1) / a) * a;
}

uint32_t record_total_size(uint32_t block_size,
                                uint32_t payload_size) {
  return align_up(static_cast<uint32_t>(sizeof(RecordHeader)) +
                      payload_size,
                  block_size);
}

uint32_t record_blocks(uint32_t block_size,
                            uint32_t payload_size) {
  return record_total_size(block_size, payload_size) / block_size;
}
