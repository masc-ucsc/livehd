// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <string_view>

#include "hhds/graph.hpp"

namespace livehd::partition {

// Structurally inline the whole instance hierarchy under `top` into ONE flat
// graph created in `lib` under `flat_name` (caller deletes it when done — it is
// a scratch def, never meant to persist). Child defs WITH a body are recursively
// inlined once per instance, with node/wire names prefixed by the dotted
// instance path (`pipeA_alu.foo`); body-less defs (liberty cells, tie cells,
// external IP, fproperty markers) stay as opaque Sub instances whose IO decls
// are cloned into `lib`. Per-node flat colors, names, luts, srcids and the
// proven/runtime_check formal markers are carried; driver pins keep
// bits/sign/pin_name/pin_offset. The top graph's coloring_info blob (the
// region_opts block-attribute channel) is copied onto the flat graph.
// Returns nullptr after a diag on an unresolvable shape (e.g. a combinational
// feedthrough cycle threading module boundaries).
[[nodiscard]] std::shared_ptr<hhds::Graph> flatten_hierarchy(hhds::Graph* top, hhds::GraphLibrary* lib,
                                                             std::string_view flat_name);

}  // namespace livehd::partition
