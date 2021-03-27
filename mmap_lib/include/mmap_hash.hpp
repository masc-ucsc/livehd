//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

namespace mmap_lib {

#include "waterhash.hpp"
#include "woothash.hpp"

static inline constexpr uint64_t hash64(const void *key, uint32_t len, uint64_t seed = 1023) { return woothash64(key, len, seed); }

static inline constexpr uint64_t hash32(const void *key, uint32_t len, uint64_t seed = 1023) { return waterhash(key, len, seed); }

}  // namespace mmap_lib
