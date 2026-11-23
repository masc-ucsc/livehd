// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace livehd::lec::bv_words {

using u64 = uint64_t;

inline uint32_t nwords(uint32_t w) { return w == 0 ? 1 : 1 + (w - 1) / 64; }
inline u64      top_mask(uint32_t w) {
  const uint32_t r = w & 63;
  return r == 0 ? ~u64{0} : ((u64{1} << r) - 1);
}
inline void clear_top(u64* d, uint32_t w) { d[nwords(w) - 1] &= top_mask(w); }
inline bool get_bit(const u64* a, uint32_t i) { return ((a[i >> 6] >> (i & 63)) & 1) != 0; }
inline void set_bit(u64* a, uint32_t i) { a[i >> 6] |= u64{1} << (i & 63); }
inline void zero(u64* d, uint32_t nw) { std::memset(d, 0, sizeof(u64) * nw); }
inline void copy(u64* d, const u64* s, uint32_t nw) { std::memcpy(d, s, sizeof(u64) * nw); }
inline bool is_zero(const u64* a, uint32_t nw) {
  for (uint32_t i = 0; i < nw; ++i) {
    if (a[i] != 0) {
      return false;
    }
  }
  return true;
}
inline bool ult(const u64* a, const u64* b, uint32_t nw) {
  for (uint32_t i = nw; i-- > 0;) {
    if (a[i] != b[i]) {
      return a[i] < b[i];
    }
  }
  return false;
}
inline void add(u64* d, const u64* a, const u64* b, uint32_t nw) {
  u64 carry = 0;
  for (uint32_t i = 0; i < nw; ++i) {
    const u64 s  = a[i] + b[i];
    const u64 c1 = s < a[i] ? 1 : 0;
    d[i]         = s + carry;
    carry        = c1 | (d[i] < s ? 1 : 0);
  }
}
inline void sub(u64* d, const u64* a, const u64* b, uint32_t nw) {
  u64 borrow = 0;
  for (uint32_t i = 0; i < nw; ++i) {
    const u64 x  = a[i] - b[i];
    const u64 b1 = a[i] < b[i] ? 1 : 0;
    d[i]         = x - borrow;
    borrow       = b1 | (x < borrow ? 1 : 0);
  }
}
inline void neg(u64* d, const u64* a, uint32_t nw) {
  u64 carry = 1;
  for (uint32_t i = 0; i < nw; ++i) {
    const u64 x = ~a[i];
    d[i]        = x + carry;
    carry       = (carry != 0 && d[i] == 0) ? 1 : 0;
  }
}
// d = a * b mod 2^(64*nw); d must not alias a or b.
inline void mul(u64* d, const u64* a, const u64* b, uint32_t nw) {
  zero(d, nw);
  for (uint32_t i = 0; i < nw; ++i) {
    if (a[i] == 0) {
      continue;
    }
    u64 carry = 0;
    for (uint32_t j = 0; i + j < nw; ++j) {
      const unsigned __int128 t = static_cast<unsigned __int128>(a[i]) * b[j] + d[i + j] + carry;
      d[i + j]                  = static_cast<u64>(t);
      carry                     = static_cast<u64>(t >> 64);
    }
  }
}
// SMT-LIB bvudiv/bvurem: x / 0 = all ones, x % 0 = x. q/r may be null.
inline void udivrem(u64* q, u64* r, const u64* a, const u64* b, uint32_t w) {
  const uint32_t nw = nwords(w);
  if (is_zero(b, nw)) {
    if (q != nullptr) {
      for (uint32_t i = 0; i < nw; ++i) {
        q[i] = ~u64{0};
      }
      clear_top(q, w);
    }
    if (r != nullptr) {
      copy(r, a, nw);
    }
    return;
  }
  if (nw == 1) {
    if (q != nullptr) {
      q[0] = a[0] / b[0];
    }
    if (r != nullptr) {
      r[0] = a[0] % b[0];
    }
    return;
  }
  if (nw == 2) {
    const auto A = (static_cast<unsigned __int128>(a[1]) << 64) | a[0];
    const auto B = (static_cast<unsigned __int128>(b[1]) << 64) | b[0];
    if (q != nullptr) {
      const auto Q = A / B;
      q[0]         = static_cast<u64>(Q);
      q[1]         = static_cast<u64>(Q >> 64);
    }
    if (r != nullptr) {
      const auto R = A % B;
      r[0]         = static_cast<u64>(R);
      r[1]         = static_cast<u64>(R >> 64);
    }
    return;
  }
  // Restoring long division, one bit at a time (wide operands are rare).
  std::vector<u64> rem(nw + 1, 0), quo(nw, 0), bb(nw + 1, 0), tmp(nw + 1, 0);
  copy(bb.data(), b, nw);
  for (uint32_t i = w; i-- > 0;) {
    for (uint32_t k = nw + 1; k-- > 0;) {
      rem[k] = (rem[k] << 1) | (k > 0 ? rem[k - 1] >> 63 : 0);
    }
    rem[0] |= get_bit(a, i) ? 1 : 0;
    if (!ult(rem.data(), bb.data(), nw + 1)) {
      sub(tmp.data(), rem.data(), bb.data(), nw + 1);
      rem.swap(tmp);
      set_bit(quo.data(), i);
    }
  }
  if (q != nullptr) {
    copy(q, quo.data(), nw);
  }
  if (r != nullptr) {
    copy(r, rem.data(), nw);
  }
}

template <class Equal>
bool all_distinct(size_t count, Equal equal) {
  for (size_t i = 1; i < count; ++i) {
    for (size_t j = 0; j < i; ++j) {
      if (equal(i, j)) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace livehd::lec::bv_words
