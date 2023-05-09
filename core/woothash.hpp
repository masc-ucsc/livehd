/*
   Based on woothash (small changes for C++)

    Woothash takes (optimally) 64-bit inputs and produces a 64-bit hash as its result.
    It is an edited version of wyhash that uses at most 64-bit math instead of 128-bit.
    Version 2 is a little slower than version 1, but the reverse is true on the JVM.
    Original Author: Wang Yi <godspeed_china@yeah.net>
    Woothash Variant Author: Tommy Ettinger <tommy.ettinger@gmail.com>
*/

#pragma once

#include <cstdint>
#include <cstring>

namespace lh {

constexpr uint64_t _wootp0 = 0xa0761d6478bd642full, _wootp1 = 0xe7037ed1a0b428dbull, _wootp2 = 0x8ebc6af09c88c6e3ull;
constexpr uint64_t _wootp3 = 0x589965cc75374cc3ull, _wootp4 = 0x1d8e4e27c47d124full, _wootp5 = 0xeb44accab455d165ull;

static inline constexpr uint64_t rotl64(uint64_t x, int8_t r) { return (x << r) | (x >> (64 - r)); }

// has an uneven rotation structure that gives resistance to when one parameter is 0, unlike wyhash' _wymum().
static inline constexpr uint64_t _wootmum(const uint64_t A, const uint64_t B) {
  uint64_t r = (A ^ rotl64(B, 39)) * (B ^ rotl64(A, 39));
  return r - (r >> 32);
}

static inline uint64_t _wootr08(const uint8_t *p) {
  uint8_t v = 0;
  std::memcpy(&v, p, 1);
  return v;
}
static inline uint64_t _wootr16(const uint8_t *p) {
  uint16_t v = 0;
  std::memcpy(&v, p, 2);
  return v;
}
static inline uint64_t _wootr32(const uint8_t *p) {
  uint32_t v = 0;
  std::memcpy(&v, p, 4);
  return v;
}
static inline uint64_t _wootr64(const uint8_t *p) {
  uint64_t v = 0;
  std::memcpy(&v, p, 8);
  return v;
}
static inline uint64_t __wootr64(const uint8_t *p) { return (_wootr32(p) << 32) | _wootr32(p + 4); }

static inline uint64_t woothash64(const void *key, uint64_t len,
                                  uint64_t seed = 1021) {  // 1021 is a small prime easy const
  const uint8_t *p = (const uint8_t *)key;
  uint64_t       a = seed ^ _wootp4;
  uint64_t       b = rotl64(seed, 17) ^ _wootp3;
  uint64_t       c = rotl64(seed, 31) ^ _wootp2;
  uint64_t       d = rotl64(seed, 47) ^ _wootp1;

  for (uint64_t i = 0; i + 32 <= len; i += 32, p += 32) {
    a = (_wootr64(p) ^ a) * _wootp1;
    a = rotl64(a, 22);
    a *= _wootp3;
    b = (_wootr64(p + 8) ^ b) * _wootp2;
    b = rotl64(b, 25);
    b *= _wootp4;
    c = (_wootr64(p + 16) ^ c) * _wootp3;
    c = rotl64(c, 28);
    c *= _wootp5;
    d = (_wootr64(p + 24) ^ d) * _wootp4;
    d = rotl64(d, 31);
    d *= _wootp1;
    seed += a + b + c + d;
  }
  seed += _wootp5;
  switch (len & 31) {
    case 1: seed = _wootmum(seed, _wootr08(p) ^ _wootp1); break;
    case 2: seed = _wootmum(seed, _wootr16(p) ^ _wootp1); break;
    case 3: seed = _wootmum(seed, ((_wootr16(p) << 8) | _wootr08(p + 2)) ^ _wootp1); break;
    case 4: seed = _wootmum(seed, _wootr32(p) ^ _wootp1); break;
    case 5: seed = _wootmum(seed, ((_wootr32(p) << 8) | _wootr08(p + 4)) ^ _wootp1); break;
    case 6: seed = _wootmum(seed, ((_wootr32(p) << 16) | _wootr16(p + 4)) ^ _wootp1); break;
    case 7: seed = _wootmum(seed, ((_wootr32(p) << 24) | (_wootr16(p + 4) << 8) | _wootr08(p + 6)) ^ _wootp1); break;
    case 8: seed = _wootmum(seed, __wootr64(p) ^ _wootp1); break;
    case 9: seed = _wootmum(__wootr64(p) + seed, _wootr08(p + 8) ^ _wootp2); break;
    case 10: seed = _wootmum(__wootr64(p) + seed, _wootr16(p + 8) ^ _wootp2); break;
    case 11: seed = _wootmum(__wootr64(p) + seed, ((_wootr16(p + 8) << 8) | _wootr08(p + 8 + 2)) ^ _wootp2); break;
    case 12: seed = _wootmum(__wootr64(p) + seed, _wootr32(p + 8) ^ _wootp2); break;
    case 13: seed = _wootmum(__wootr64(p) + seed, ((_wootr32(p + 8) << 8) | _wootr08(p + 8 + 4)) ^ _wootp2); break;
    case 14: seed = _wootmum(__wootr64(p) + seed, ((_wootr32(p + 8) << 16) | _wootr16(p + 8 + 4)) ^ _wootp2); break;
    case 15:
      seed = _wootmum(__wootr64(p) + seed, ((_wootr32(p + 8) << 24) | (_wootr16(p + 8 + 4) << 8) | _wootr08(p + 8 + 6)) ^ _wootp2);
      break;
    case 16: seed = _wootmum(__wootr64(p) + seed, __wootr64(p + 8) + _wootp2); break;
    case 17:
      seed = _wootmum(__wootr64(p) + seed, _wootr32(p + 8) + _wootp2)
             + _wootmum(_wootr32(p + 12) ^ seed, _wootr08(p + 16) ^ _wootp3);
      break;
    case 18:
      seed = _wootmum(__wootr64(p) + seed, _wootr32(p + 8) + _wootp2)
             + _wootmum(_wootr32(p + 12) ^ seed, _wootr16(p + 16) ^ _wootp3);
      break;
    case 19:
      seed = _wootmum(__wootr64(p) + seed, _wootr32(p + 8) + _wootp2)
             + _wootmum(_wootr32(p + 12) ^ seed, ((_wootr16(p + 16) << 8) | _wootr08(p + 16 + 2)) ^ _wootp3);
      break;
    case 20:
      seed = _wootmum(__wootr64(p) + seed, _wootr32(p + 8) + _wootp2)
             + _wootmum(_wootr32(p + 12) ^ seed, _wootr32(p + 16) ^ _wootp3);
      break;
    case 21:
      seed = _wootmum(__wootr64(p) + seed, __wootr64(p + 8) + _wootp2)
             + _wootmum(_wootr16(p + 16) ^ seed, ((_wootr16(p + 18) << 8) | _wootr08(p + 16 + 4)) ^ _wootp3);
      break;
    case 22:
      seed = _wootmum(__wootr64(p) + seed, __wootr64(p + 8) + _wootp2)
             + _wootmum(_wootr16(p + 16) ^ seed, (_wootr32(p + 18) << 16) ^ _wootp3);
      break;
    case 23:
      seed = _wootmum(__wootr64(p) + seed, __wootr64(p + 8) + _wootp2)
             + _wootmum(_wootp4 + seed, ((_wootr32(p + 16) << 24) | (_wootr16(p + 16 + 4) << 8) | _wootr08(p + 16 + 6)) ^ _wootp3);
      break;
    case 24:
      seed = _wootmum(__wootr64(p) + seed, __wootr64(p + 8) + _wootp2) + _wootmum(__wootr64(p + 16) + seed, seed ^ _wootp3);
      break;
    case 25:
      seed = _wootmum(__wootr64(p) + seed, __wootr64(p + 8) + _wootp2)
             + _wootmum(__wootr64(p + 16) ^ seed, _wootr08(p + 24) ^ _wootp4);
      break;
    case 26:
      seed = _wootmum(__wootr64(p) + seed, __wootr64(p + 8) + _wootp2)
             + _wootmum(__wootr64(p + 16) ^ seed, _wootr16(p + 24) ^ _wootp4);
      break;
    case 27:
      seed = _wootmum(__wootr64(p) + seed, __wootr64(p + 8) + _wootp2)
             + _wootmum(__wootr64(p + 16) ^ seed, ((_wootr16(p + 24) << 8) | _wootr08(p + 24 + 2)) ^ _wootp4);
      break;
    case 28:
      seed = _wootmum(__wootr64(p) + seed, __wootr64(p + 8) + _wootp2)
             + _wootmum(__wootr64(p + 16) ^ seed, _wootr32(p + 24) ^ _wootp4);
      break;
    case 29:
      seed = _wootmum(__wootr64(p) + seed, __wootr64(p + 8) + _wootp2)
             + _wootmum(__wootr64(p + 16) ^ seed, ((_wootr32(p + 24) << 8) | _wootr08(p + 24 + 4)) ^ _wootp4);
      break;
    case 30:
      seed = _wootmum(__wootr64(p) + seed, __wootr64(p + 8) + _wootp2)
             + _wootmum(__wootr64(p + 16) ^ seed, ((_wootr32(p + 24) << 16) | _wootr16(p + 24 + 4)) ^ _wootp4);
      break;
    case 31:
      seed = _wootmum(__wootr64(p) + seed, __wootr64(p + 8) + _wootp2)
             + _wootmum(__wootr64(p + 16) ^ seed,
                        ((_wootr32(p + 24) << 24) | (_wootr16(p + 24 + 4) << 8) | _wootr08(p + 24 + 6)) ^ _wootp4);
      break;
  }
  seed = (seed ^ seed << 16) * (len ^ _wootp0 ^ seed >> 32);
  return seed - (seed >> 31) + (seed << 33);
}

static inline constexpr uint32_t woothash32(const void *key, uint64_t len, uint64_t seed = 1021) {
  const uint8_t *p = (const uint8_t *)key;
  uint64_t       a = seed ^ _wootp4;
  uint64_t       b = rotl64(seed, 17) ^ _wootp3;
  uint64_t       c = rotl64(seed, 31) ^ _wootp2;
  uint64_t       d = rotl64(seed, 47) ^ _wootp1;

  for (uint64_t i = 0; i + 32 <= len; i += 32, p += 32) {
    a = (_wootr64(p) ^ a) * _wootp1;
    a = rotl64(a, 22);
    a *= _wootp3;
    b = (_wootr64(p + 8) ^ b) * _wootp2;
    b = rotl64(b, 25);
    b *= _wootp4;
    c = (_wootr64(p + 16) ^ c) * _wootp3;
    c = rotl64(c, 28);
    c *= _wootp5;
    d = (_wootr64(p + 24) ^ d) * _wootp4;
    d = rotl64(d, 31);
    d *= _wootp1;
    seed += a + b + c + d;
  }
  seed += _wootp5;
  switch (len & 31) {
    case 1: seed = _wootmum(seed, _wootr08(p) ^ _wootp1); break;
    case 2: seed = _wootmum(seed, _wootr16(p) ^ _wootp1); break;
    case 3: seed = _wootmum(seed, ((_wootr16(p) << 8) | _wootr08(p + 2)) ^ _wootp1); break;
    case 4: seed = _wootmum(seed, _wootr32(p) ^ _wootp1); break;
    case 5: seed = _wootmum(seed, ((_wootr32(p) << 8) | _wootr08(p + 4)) ^ _wootp1); break;
    case 6: seed = _wootmum(seed, ((_wootr32(p) << 16) | _wootr16(p + 4)) ^ _wootp1); break;
    case 7: seed = _wootmum(seed, ((_wootr32(p) << 24) | (_wootr16(p + 4) << 8) | _wootr08(p + 6)) ^ _wootp1); break;
    case 8: seed = _wootmum(seed, __wootr64(p) ^ _wootp1); break;
    case 9: seed = _wootmum(__wootr64(p) + seed, _wootr08(p + 8) ^ _wootp2); break;
    case 10: seed = _wootmum(__wootr64(p) + seed, _wootr16(p + 8) ^ _wootp2); break;
    case 11: seed = _wootmum(__wootr64(p) + seed, ((_wootr16(p + 8) << 8) | _wootr08(p + 8 + 2)) ^ _wootp2); break;
    case 12: seed = _wootmum(__wootr64(p) + seed, _wootr32(p + 8) ^ _wootp2); break;
    case 13: seed = _wootmum(__wootr64(p) + seed, ((_wootr32(p + 8) << 8) | _wootr08(p + 8 + 4)) ^ _wootp2); break;
    case 14: seed = _wootmum(__wootr64(p) + seed, ((_wootr32(p + 8) << 16) | _wootr16(p + 8 + 4)) ^ _wootp2); break;
    case 15:
      seed = _wootmum(__wootr64(p) + seed, ((_wootr32(p + 8) << 24) | (_wootr16(p + 8 + 4) << 8) | _wootr08(p + 8 + 6)) ^ _wootp2);
      break;
    case 16: seed = _wootmum(__wootr64(p) + seed, __wootr64(p + 8) + _wootp2); break;
    case 17:
      seed = _wootmum(__wootr64(p) + seed, _wootr32(p + 8) + _wootp2)
             + _wootmum(_wootr32(p + 12) ^ seed, _wootr08(p + 16) ^ _wootp3);
      break;
    case 18:
      seed = _wootmum(__wootr64(p) + seed, _wootr32(p + 8) + _wootp2)
             + _wootmum(_wootr32(p + 12) ^ seed, _wootr16(p + 16) ^ _wootp3);
      break;
    case 19:
      seed = _wootmum(__wootr64(p) + seed, _wootr32(p + 8) + _wootp2)
             + _wootmum(_wootr32(p + 12) ^ seed, ((_wootr16(p + 16) << 8) | _wootr08(p + 16 + 2)) ^ _wootp3);
      break;
    case 20:
      seed = _wootmum(__wootr64(p) + seed, _wootr32(p + 8) + _wootp2)
             + _wootmum(_wootr32(p + 12) ^ seed, _wootr32(p + 16) ^ _wootp3);
      break;
    case 21:
      seed = _wootmum(__wootr64(p) + seed, __wootr64(p + 8) + _wootp2)
             + _wootmum(_wootr16(p + 16) ^ seed, ((_wootr16(p + 18) << 8) | _wootr08(p + 16 + 4)) ^ _wootp3);
      break;
    case 22:
      seed = _wootmum(__wootr64(p) + seed, __wootr64(p + 8) + _wootp2)
             + _wootmum(_wootr16(p + 16) ^ seed, (_wootr32(p + 18) << 16) ^ _wootp3);
      break;
    case 23:
      seed = _wootmum(__wootr64(p) + seed, __wootr64(p + 8) + _wootp2)
             + _wootmum(_wootp4 + seed, ((_wootr32(p + 16) << 24) | (_wootr16(p + 16 + 4) << 8) | _wootr08(p + 16 + 6)) ^ _wootp3);
      break;
    case 24:
      seed = _wootmum(__wootr64(p) + seed, __wootr64(p + 8) + _wootp2)
             + _wootmum(_wootr32(p + 16) + seed, _wootr32(p + 16 + 4) ^ _wootp3);
      break;
    case 25:
      seed = _wootmum(__wootr64(p) + seed, __wootr64(p + 8) + _wootp2)
             + _wootmum(__wootr64(p + 16) ^ seed, _wootr08(p + 24) ^ _wootp4);
      break;
    case 26:
      seed = _wootmum(__wootr64(p) + seed, __wootr64(p + 8) + _wootp2)
             + _wootmum(__wootr64(p + 16) ^ seed, _wootr16(p + 24) ^ _wootp4);
      break;
    case 27:
      seed = _wootmum(__wootr64(p) + seed, __wootr64(p + 8) + _wootp2)
             + _wootmum(__wootr64(p + 16) ^ seed, ((_wootr16(p + 24) << 8) | _wootr08(p + 24 + 2)) ^ _wootp4);
      break;
    case 28:
      seed = _wootmum(__wootr64(p) + seed, __wootr64(p + 8) + _wootp2)
             + _wootmum(__wootr64(p + 16) ^ seed, _wootr32(p + 24) ^ _wootp4);
      break;
    case 29:
      seed = _wootmum(__wootr64(p) + seed, __wootr64(p + 8) + _wootp2)
             + _wootmum(__wootr64(p + 16) ^ seed, ((_wootr32(p + 24) << 8) | _wootr08(p + 24 + 4)) ^ _wootp4);
      break;
    case 30:
      seed = _wootmum(__wootr64(p) + seed, __wootr64(p + 8) + _wootp2)
             + _wootmum(__wootr64(p + 16) ^ seed, ((_wootr32(p + 24) << 16) | _wootr16(p + 24 + 4)) ^ _wootp4);
      break;
    case 31:
      seed = _wootmum(__wootr64(p) + seed, __wootr64(p + 8) + _wootp2)
             + _wootmum(__wootr64(p + 16) ^ seed,
                        ((_wootr32(p + 24) << 24) | (_wootr16(p + 24 + 4) << 8) | _wootr08(p + 24 + 6)) ^ _wootp4);
      break;
  }
  seed = (seed ^ seed << 16) * (len ^ _wootp0 ^ seed >> 32);
  return (uint32_t)(seed - (seed >> 32));
}

};  // namespace lh
