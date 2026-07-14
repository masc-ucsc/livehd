// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Flat coloring: mark every partitionable node in the def with a single color
// (id 1). The pass driver runs this per unique def in the hierarchy, so under
// `--top` the whole instance tree (or just the top def when pass.color.hier=false)
// ends up one color -- the coloring equivalent of flattening the design into a
// single region. `pass.color.continuous` is forced off here: splitting the lone
// color back into connected regions would defeat the "everything is one color"
// intent. (Source-seeded block regions still win, per apply_coloring.)

#include "color_common.hpp"
#include "hhds/graph.hpp"

namespace livehd::color {

class Color_flat {
public:
  explicit Color_flat(Color_opts opts);
  void label(hhds::Graph* g);

private:
  Color_opts opts;
};

}  // namespace livehd::color
