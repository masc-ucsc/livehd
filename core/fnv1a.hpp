// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <string_view>

namespace livehd::hash_util {

// Canonical 64-bit FNV-1a parameters. Every digest built on these is an
// internal cache key or uniquifier, so a parameter change only costs a
// one-time cache refill — nothing persists across versions by hash value.
inline constexpr uint64_t kFnv1a64_offset = 0xcbf29ce484222325ULL;  // 14695981039346656037
inline constexpr uint64_t kFnv1a64_prime  = 0x100000001b3ULL;       // 1099511628211

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

}  // namespace livehd::hash_util
