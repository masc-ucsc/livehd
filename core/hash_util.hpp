// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <cstdint>
#include <string_view>

namespace livehd::hash_util {

// Canonical 64-bit FNV-1a parameters. Several pre-consolidation copies used a
// truncated offset basis (1469598103934665603, the canonical value with its
// last digit dropped); every digest built on these is an internal cache key or
// uniquifier, so standardizing on the canonical basis only costs a one-time
// cache refill.
inline constexpr uint64_t kFnv1a64_offset = 0xcbf29ce484222325ULL;  // 14695981039346656037
inline constexpr uint64_t kFnv1a64_prime  = 0x100000001b3ULL;       // 1099511628211

// MurmurHash3's 64-bit finalizer. This is a deterministic mixer for internal
// digests; it is not a cryptographic hash.
[[nodiscard]] constexpr uint64_t mix64(uint64_t value) {
  value ^= value >> 33U;
  value *= 0xff51afd7ed558ccdULL;
  value ^= value >> 33U;
  value *= 0xc4ceb9fe1a85ec53ULL;
  value ^= value >> 33U;
  return value;
}

[[nodiscard]] constexpr uint64_t combine64(uint64_t hash, uint64_t value) {
  return mix64(hash ^ (value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U)));
}

// FNV-1a over `text`. Pass a previous return as `seed` to chain fragments:
// fnv1a64("bc", fnv1a64("a")) == fnv1a64("abc").
[[nodiscard]] constexpr uint64_t fnv1a64(std::string_view text, uint64_t seed = kFnv1a64_offset) {
  uint64_t hash = seed;
  for (char ch : text) {
    hash ^= static_cast<unsigned char>(ch);
    hash *= kFnv1a64_prime;
  }
  return hash;
}

// FNV-1a over the 8 little-endian bytes of `value`, so a word-fed digest
// matches the equivalent byte-fed one. No default seed: every word-fold use
// chains an existing digest.
[[nodiscard]] constexpr uint64_t fnv1a64_u64(uint64_t value, uint64_t seed) {
  uint64_t hash = seed;
  for (unsigned byte = 0; byte < 8U; ++byte) {
    hash ^= (value >> (byte * 8U)) & 0xffU;
    hash *= kFnv1a64_prime;
  }
  return hash;
}

}  // namespace livehd::hash_util
