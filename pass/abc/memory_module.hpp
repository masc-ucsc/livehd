// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "hhds/graph.hpp"

namespace livehd::abc {
// pass.abc.memory: how a Memory node is realized.
//   Never  (`false`) keep every memory a native instance.
//   Always (`true`)  lower every memory's RTL and map its body, whatever its
//                    size -- memory_max_bits is not consulted.
//   Auto   (`auto`, the default) fold only what a macro would never be: storage
//                    at or below memory_max_bits, or more ports than any SRAM
//                    offers (kAutoFoldPortsAbove).
enum class Memory_fold : uint8_t { Never = 0, Always = 1, Auto = 2 };

// `auto` folds a memory with MORE than this many ports whatever its storage
// size: no SRAM compiler or standard-cell library offers such a macro, so
// flops + decode is the only realization it can have. A native instance would
// just push the same bit-blast onto whoever reads the netlist.
inline constexpr uint64_t kAutoFoldPortsAbove = 3;

// The `pass.abc.memory` spelling of a mode, and its inverse (nullopt = the
// value is not one of the three).
[[nodiscard]] constexpr std::string_view memory_fold_name(Memory_fold mode) {
  switch (mode) {
    case Memory_fold::Never: return "false";
    case Memory_fold::Always: return "true";
    case Memory_fold::Auto: return "auto";
  }
  return "auto";
}

[[nodiscard]] constexpr std::optional<Memory_fold> parse_memory_fold(std::string_view v) {
  if (v == "auto") {
    return Memory_fold::Auto;
  }
  if (v == "false" || v == "0" || v == "off" || v == "no") {
    return Memory_fold::Never;
  }
  if (v == "true" || v == "1" || v == "on" || v == "yes") {
    return Memory_fold::Always;
  }
  return std::nullopt;
}

// Replace memories selected for lowering with named child modules. Native
// memories whose cgen form is an inline array are also enclosed in a module.
// The returned definitions belong to the parents' scratch graph library.
// `max_bits` (0 = no limit) is the `auto` storage threshold only.
std::vector<std::shared_ptr<hhds::Graph>> build_memory_modules(const std::vector<std::shared_ptr<hhds::Graph>>& graphs,
                                                               Memory_fold mode, uint64_t max_bits);
}  // namespace livehd::abc
