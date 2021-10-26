//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

// TODO: Move this as a callback registration for Graph_Library
// Then all the files are distributed per pass as needed

// TODO: We have attributes per node/pin/edge, we should have also per Lgraph module (lef attributes)
#include "ann_file_loc.hpp"
#include "ann_place.hpp"
#include "attribute.hpp"
#include "node.hpp"
#include "node_pin.hpp"

struct Ann_name {
  static constexpr char delay[]     = "delay";
  static constexpr char unsign[]    = "unsign";
  static constexpr char offset[]    = "offset";
  static constexpr char pin_name[]  = "pin_name";
  static constexpr char prp_vname[] = "prp_vname";
  static constexpr char ssa[]       = "ssa";

  static constexpr char nodename[]  = "nodename";
  static constexpr char instname[]  = "instname";
  static constexpr char nodeplace[] = "nodeplace";
  static constexpr char file_loc[]  = "file_loc";
  static constexpr char color[]     = "color";
};

using Ann_node_pin_offset = Attribute<Ann_name::offset, Node_pin, mmap_lib::map<Node_pin::Compact_class_driver, Bits_t> >;

using Ann_node_pin_name
    = Attribute<Ann_name::pin_name, Node_pin, mmap_lib::bimap<Node_pin::Compact_class_driver, mmap_lib::str> >;

using Ann_node_pin_prp_vname
    = Attribute<Ann_name::prp_vname, Node_pin, mmap_lib::map<Node_pin::Compact_class_driver, mmap_lib::str> >;

using Ann_node_pin_ssa = Attribute<Ann_name::ssa, Node_pin, mmap_lib::map<Node_pin::Compact_class_driver, uint32_t> >;

using Ann_node_pin_delay = Attribute<Ann_name::delay, Node_pin, mmap_lib::map<Node_pin::Compact_driver, float> >;

using Ann_node_pin_unsign = Attribute<Ann_name::unsign, Node_pin, mmap_lib::map<Node_pin::Compact_driver, bool> >;

using Ann_node_name = Attribute<Ann_name::nodename, Node, mmap_lib::bimap<Node::Compact_class, mmap_lib::str> >;

using Ann_inst_name = Attribute<Ann_name::instname, Node, mmap_lib::map<Node::Compact, mmap_lib::str> >;

using Ann_node_place = Attribute<Ann_name::nodeplace, Node, mmap_lib::map<Node::Compact, Ann_place> >;

using Ann_node_file_loc = Attribute<Ann_name::file_loc, Node, mmap_lib::map<Node::Compact_class, Ann_file_loc> >;

using Ann_node_color = Attribute<Ann_name::color, Node, mmap_lib::bimap<Node::Compact_class, int32_t> >;

struct Ann_support {
  // TODO: Change to object to register annotations, and have an "update" for incremental
  static void clear(Lgraph *lg) {
    Ann_node_pin_delay::clear(lg);
    Ann_node_pin_unsign::clear(lg);
    Ann_node_pin_offset::clear(lg);
    Ann_node_pin_name::clear(lg);
    Ann_node_pin_prp_vname::clear(lg);
    Ann_node_pin_ssa::clear(lg);

    Ann_node_name::clear(lg);
    Ann_inst_name::clear(lg);
    Ann_node_place::clear(lg);
    Ann_node_file_loc::clear(lg);
    Ann_node_color::clear(lg);
  };

  static void sync(Lgraph *lg) {
    Ann_node_pin_delay::sync(lg);
    Ann_node_pin_unsign::sync(lg);
    Ann_node_pin_offset::sync(lg);
    Ann_node_pin_name::sync(lg);
    Ann_node_pin_prp_vname::sync(lg);
    Ann_node_pin_ssa::sync(lg);

    Ann_node_name::sync(lg);
    Ann_inst_name::sync(lg);
    Ann_node_place::sync(lg);
    Ann_node_file_loc::sync(lg);
    Ann_node_color::sync(lg);
  };
};
