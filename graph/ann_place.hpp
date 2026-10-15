// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cmath>
#include <cstdint>

// Persisted physical rectangle. All lengths use the owning technology's DBU.
class Ann_place {
public:
  enum class Status : uint8_t { UNPLACED, PLACED, FIXED, LOCKED };
  enum class Orientation : uint8_t { N, W, S, E, FN, FW, FS, FE };
  int64_t     x = 0, y = 0, width = 0, height = 0;
  Orientation orientation = Orientation::N;
  Status      status      = Status::UNPLACED;
  constexpr Ann_place()   = default;
  Ann_place(double px, double py, double w, double h) { replace(px, py, w, h); }
  void replace(double px, double py, double w, double h) {
    x      = std::llround(px);
    y      = std::llround(py);
    width  = std::llround(w);
    height = std::llround(h);
  }
  friend bool operator==(const Ann_place&, const Ann_place&) = default;
  bool        is_valid() const { return width > 0 && height > 0; }
};
