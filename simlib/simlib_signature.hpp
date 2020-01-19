//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#ifdef SIMLIB_TRACE
#include <functional>
#include <vector>

class Simlib_signature {
private:
  uint64_t h;
public:
  Simlib_signature() {
    h = 0;
  }

  void append(uint64_t d) {
    h ^= std::hash<uint64_t>{}(d);
  }

  uint64_t *get_map_address() {
    return &h;
  }

  size_t get_map_bytes() const {
    return sizeof(uint64_t);
  }

  bool operator!=(const Simlib_signature &s2) const {
    return h != s2.h;
  }

};
#endif
