// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>

namespace livehd::sim {
using Native_function = void (*)(uint64_t*, void**);

// Native, same-target object ABI. Only the descriptor is exported. Its entry
// function and sparse initialization table have object-local linkage. Mutable
// storage is allocated by the runtime per instance, never in process globals.
inline constexpr uint64_t native_state_abi = 1;
struct Native_initial_word {
  uint64_t word;
  uint64_t value;
};
struct Native_state_image {
  uint64_t                   abi;
  uint64_t                   words;
  uint64_t                   public_words;
  uint64_t                   initial_count;
  const Native_initial_word* initial;
  Native_function            entry;
};
}  // namespace livehd::sim
