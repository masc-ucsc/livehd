//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>

class __attribute__((packed)) Ann_file_loc {
protected:
  uint8_t  length;
  uint8_t  start_hchunk;
  uint16_t start_lchunk;

public:
  Ann_file_loc(uint32_t loc_start, uint32_t loc_length)
      : length((uint8_t)loc_length), start_hchunk((uint8_t)loc_start >> 16), start_lchunk((uint16_t)loc_start) {}

  uint32_t get_start() const { return (((uint32_t)start_hchunk) << 16) | start_lchunk; }
  uint32_t get_length() const { return length; }
};

