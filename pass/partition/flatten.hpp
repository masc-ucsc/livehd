// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "hhds/graph.hpp"

namespace livehd::partition {

// Resolve `inst`'s child def in `outlib`, cloning the IO decl when the def is a
// body-less black box (liberty/tie cell, external IP, fproperty marker) so the
// instance stays opaque. Returns nullptr when the child HAS a body and is not in
// `outlib` yet -- children-before-parents ordering should have emitted it first.
//
// Lives here rather than in pass_partition so the flattener is a leaf library:
// pass.color needs to flatten (virtual flattening) and pass.partition already
// depends on pass.color, so flatten cannot reach back up.
// NOT [[nodiscard]]: several callers invoke it purely for the side effect of
// cloning a black-box decl into their scratch library.
std::shared_ptr<hhds::GraphIO> resolve_or_clone_subdef(hhds::GraphLibrary* outlib, const hhds::Node_class& inst);

// Where a flat node came from. Two instances of one def clone that def's body
// twice, so two distinct flat nodes share one origin -- which is exactly the
// ambiguity a caller writing per-def data back has to resolve.
struct Flat_origin {
  hhds::Gid        def_gid = 0;
  hhds::Node_class src_node;
};
using Flat_origin_map = absl::flat_hash_map<hhds::Node_class, Flat_origin>;

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
// `origin`, when non-null, is filled with one entry per CLONED flat node giving
// the def and the node it was cloned from (see Flat_origin).
// With preserve_modules, definitions marked attrs::memory_module or
// attrs::ware_module remain instances. Their bodies must already exist in lib (children-first emission).
[[nodiscard]] std::shared_ptr<hhds::Graph> flatten_hierarchy(hhds::Graph* top, hhds::GraphLibrary* lib, std::string_view flat_name,
                                                             Flat_origin_map* origin = nullptr, bool preserve_modules = false);

}  // namespace livehd::partition
