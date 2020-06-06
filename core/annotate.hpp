//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

// TODO: Move this as a callback registration for Graph_Library
// Then all the files are distributed per pass as needed

// TODO: We have attributes per node/pin/edge, we should have also per lgraph module (lef attributes)
#include "ann_ssa.hpp"
#include "ann_bitwidth.hpp"
#include "ann_file_loc.hpp"
#include "ann_place.hpp"
#include "attribute.hpp"

struct Ann_name {
  static constexpr char delay[]      = "delay";
  static constexpr char wireoffset[] = "wireoffset";
  static constexpr char wirename[]   = "wirename";
  static constexpr char nodename[]   = "nodename";
  static constexpr char nodeplace[]  = "nodeplace";
  static constexpr char cfgmeta[]    = "cfgmeta";
  static constexpr char bitwidth[]   = "bitwidth";
  static constexpr char file_loc[]   = "file_loc";
  static constexpr char tree_pos[]   = "tree_pos";
  static constexpr char color[]      = "color";
  static constexpr char ssa[]        = "ssa";
};

using Ann_node_pin_offset = Attribute<Ann_name::wireoffset, Node_pin, mmap_lib::map<Node_pin::Compact_class_driver, uint16_t> >;

using Ann_node_pin_name = Attribute<Ann_name::wirename, Node_pin, mmap_lib::bimap<Node_pin::Compact_class_driver, std::string_view> >;

using Ann_node_pin_bitwidth = Attribute<Ann_name::bitwidth, Node_pin, mmap_lib::map<Node_pin::Compact_driver, Ann_bitwidth> >;
using Ann_node_pin_ssa      = Attribute<Ann_name::ssa,      Node_pin, mmap_lib::map<Node_pin::Compact_driver, Ann_ssa> >;

using Ann_node_pin_delay = Attribute<Ann_name::delay, Node_pin, mmap_lib::map<Node_pin::Compact_driver, float> >;

using Ann_node_name = Attribute<Ann_name::nodename, Node, mmap_lib::bimap<Node::Compact_class, std::string_view> >;

using Ann_node_cfgmeta = Attribute<Ann_name::cfgmeta, Node, mmap_lib::map<Node::Compact_class, std::string_view> >;

using Ann_node_place = Attribute<Ann_name::nodeplace, Node, mmap_lib::map<Node::Compact, Ann_place> >;

using Ann_node_file_loc = Attribute<Ann_name::file_loc, Node, mmap_lib::map<Node::Compact_class, Ann_file_loc> >;

using Ann_node_tree_pos = Attribute<Ann_name::tree_pos, Node, mmap_lib::map<Node::Compact_class, uint32_t> >;

using Ann_node_color = Attribute<Ann_name::color, Node, mmap_lib::bimap<Node::Compact_class, std::string_view> >;

struct Ann_support {
  // TODO: Change to object to register annotations, and have an "update" for incremental
  static void clear(LGraph *lg) {
    Ann_node_pin_offset::clear(lg);
    Ann_node_pin_delay::clear(lg);
    Ann_node_pin_name::clear(lg);
    Ann_node_pin_bitwidth::clear(lg);
    Ann_node_pin_delay::clear(lg);

    Ann_node_name::clear(lg);
    Ann_node_place::clear(lg);
    Ann_node_cfgmeta::clear(lg);
    Ann_node_file_loc::clear(lg);
    Ann_node_tree_pos::clear(lg);
    Ann_node_color::clear(lg);
  };
};
