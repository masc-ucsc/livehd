// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace livehd {
// Decimal text of a value held in little-endian 64-bit words.
[[nodiscard]] inline std::string wide_decimal(const uint64_t* w, unsigned bits, bool sign = true) {
  if (bits == 0) {
    return "0";
  }
  const size_t          n = (bits + 63) / 64;
  std::vector<uint64_t> v(w, w + n);
  const bool            neg = sign && ((w[(bits - 1) / 64] >> ((bits - 1) % 64)) & 1U);
  if (bits % 64 != 0) {  // the words are sign-extended over the last word: drop the extension
    v[n - 1] &= (uint64_t{1} << (bits % 64)) - 1;
  }
  if (neg) {  // magnitude: ~v + 1 within `bits`
    for (auto& x : v) {
      x = ~x;
    }
    if (bits % 64 != 0) {
      v[n - 1] &= (uint64_t{1} << (bits % 64)) - 1;
    }
    for (auto& x : v) {
      if (++x != 0) {
        break;
      }
    }
    if (bits % 64 != 0) {
      v[n - 1] &= (uint64_t{1} << (bits % 64)) - 1;
    }
  }
  std::string out;
  for (;;) {
    unsigned __int128 rem = 0;
    bool              any = false;
    for (size_t i = n; i-- > 0;) {
      const unsigned __int128 cur = (rem << 64) | v[i];
      v[i]                        = static_cast<uint64_t>(cur / 10);
      rem                         = cur % 10;
      any                         = any || v[i] != 0;
    }
    out.push_back(static_cast<char>('0' + static_cast<int>(rem)));
    if (!any) {
      break;
    }
  }
  if (neg) {
    out.push_back('-');
  }
  return std::string(out.rbegin(), out.rend());
}

}  // namespace livehd
