// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "hhds/graph.hpp"

namespace livehd::partition {

// A color is one flat graph, and a color may contain another color as a SUBMODULE: a synthesis group whose path
// extends another group's path (`::[color=...]` inside a colored block) is its own region module, instantiated by its
// parent's region module -- never inlined into it, and never a sibling of it in the def's wrapper.
//
// pass.partition decomposes a def into one module per region plus a wrapper that instantiates every region; it records
// the parent of each nested region on the wrapper (attrs::region_nesting, set by record_region_nesting). nest_regions
// then moves each child region's instance from the wrapper into its parent region's module, deepest first:
//
//   * an edge between parent and child becomes a wire inside the parent (the parent port it used is deleted when nothing
//     else reads it);
//   * an edge between the child and anything outside the parent (a primary IO, another region) becomes a new parent
//     port `<child>__<port>` that forwards it;
//   * a constant driving the child is re-created inside the parent.
//
// It runs on the FINAL library of a run (the partitioned or the mapped netlist), after any region cache has persisted
// its bodies, so cached region bodies are never the nested ones. Returns the number of child instances moved; pairs
// whose modules are not both instantiated (exactly once) by the wrapper are skipped. The attribute is cleared.
size_t nest_regions(hhds::GraphLibrary& lib, hhds::Graph& top);

// One region of a decomposed def, as the color index (<workdir>/synth/color_index.json) records it: the region module,
// its color and synthesis-group path (empty for an automatic color), its parent region module (empty at top level),
// and the source files its logic came from.
struct Region_entry {
  std::string              module;
  int                      color = 0;
  std::vector<std::string> path;
  std::string              parent;
  std::vector<std::string> files;
};

// Record on the wrapper what nest_regions applies (the child/parent pairs, deepest child first) and the def's region
// entries (attrs::region_nesting, JSON {"nesting":[[child,parent],...],"regions":[...]}).
void record_region_index(hhds::Graph& top, const std::vector<std::pair<std::string, std::string>>& child_parent,
                         const std::vector<Region_entry>& regions);
// The region entries recorded on `top` (empty when none). Read them before nest_regions clears the attribute.
std::vector<Region_entry> region_entries(hhds::Graph& top);

}  // namespace livehd::partition
