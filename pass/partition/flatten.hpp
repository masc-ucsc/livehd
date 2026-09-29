// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <string_view>
#include <unordered_set>

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

struct Flat_instance_path;

// Where a flat node came from, including its instance path. Paths are shared
// by every node cloned from one context instead of copied into each origin.
struct Flat_origin {
  hhds::Gid                                 def_gid = 0;
  hhds::Node_class                          src_node;
  std::shared_ptr<const Flat_instance_path> instance;

  [[nodiscard]] int32_t color() const;
  void                  set_color(int32_t color) const;
};
using Flat_origin_map = absl::flat_hash_map<hhds::Node_class, Flat_origin>;

// Structurally inline the whole instance hierarchy under `top` into ONE flat
// graph created in `lib` under `flat_name` (caller deletes it when done — it is
// a scratch def, never meant to persist). Child defs WITH a body are recursively
// inlined once per instance, with node/wire names prefixed by the dotted
// instance path (`pipeA_alu.foo`); body-less defs (liberty cells, tie cells,
// external IP, fproperty markers) stay as opaque Sub instances whose IO decls
// are cloned into `lib`. Occurrence colors (falling back to definition colors),
// names, luts, srcids and proven/runtime_check formal markers are carried; driver pins keep
// bits/sign/pin_name/pin_offset. The top graph's coloring_info blob (the
// region_opts block-attribute channel) is copied onto the flat graph.
// Returns nullptr after a diag on an unresolvable shape (e.g. a combinational
// feedthrough cycle threading module boundaries).
// `origin`, when non-null, is filled with one entry per CLONED flat node giving
// the def and the node it was cloned from (see Flat_origin).
// With preserve_modules, definitions marked attrs::memory_module or
// attrs::ware_module remain instances. The pass-local preserved_defs set adds
// boundaries, including compact loop bodies. Their mapped bodies must already
// exist in lib (children-first emission).
[[nodiscard]] std::shared_ptr<hhds::Graph> flatten_hierarchy(hhds::Graph* top, hhds::GraphLibrary* lib, std::string_view flat_name,
                                                             Flat_origin_map* origin = nullptr, bool preserve_modules = false,
                                                             const std::unordered_set<hhds::Gid>& preserved_defs = {});

}  // namespace livehd::partition
