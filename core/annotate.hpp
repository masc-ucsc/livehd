//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

// TODO: Move this as a callback registration for Graph_Library
// Then all the files are distributed per pass as needed

#include "attribute_sview.hpp"
#include "attribute_data.hpp"

#include "ann_place.hpp"
#include "ann_bitwidth.hpp"
#include "ann_file_loc.hpp"

struct Ann_name {
  static constexpr char offset[]    = "offset";
  static constexpr char delay[]     = "delay";
  static constexpr char wirename[]  = "wirename";
  static constexpr char nodename[]  = "nodename";
  static constexpr char nodeplace[] = "nodeplace";
  static constexpr char cfgmeta[]   = "cfgmeta";
  static constexpr char bitwidth[]  = "bitwidth";
  static constexpr char file_loc[]  = "file_loc";
};

using Ann_node_pin_offset   = Attribute_data< Ann_name::wirename, Node_pin, Node_pin::Compact_class, uint16_t    , Node_pin_mode::Driver>;
using Ann_node_pin_name     = Attribute_sview<Ann_name::wirename, Node_pin, Node_pin::Compact_class, true        , Node_pin_mode::Driver>;

using Ann_node_pin_bitwidth = Attribute_data< Ann_name::bitwidth, Node_pin, Node_pin::Compact      , Ann_bitwidth, Node_pin_mode::Driver>;
using Ann_node_pin_delay    = Attribute_data< Ann_name::delay   , Node_pin, Node_pin::Compact      , float       , Node_pin_mode::Driver>;

using Ann_node_name       = Attribute_sview<Ann_name::nodename , Node, Node::Compact_class, true>;
using Ann_node_cfgmeta    = Attribute_sview<Ann_name::cfgmeta  , Node, Node::Compact_class, false>;

using Ann_node_place      = Attribute_data< Ann_name::nodeplace, Node, Node::Compact      , Ann_place>;
using Ann_node_file_loc   = Attribute_data< Ann_name::file_loc , Node, Node::Compact      , Ann_file_loc>;

struct Ann_support {
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
  };

  static void sync(LGraph *lg) {
    Ann_node_pin_offset::sync(lg);
    Ann_node_pin_delay::sync(lg);
    Ann_node_pin_name::sync(lg);
    Ann_node_pin_bitwidth::sync(lg);
    Ann_node_pin_delay::sync(lg);

    Ann_node_name::sync(lg);
    Ann_node_place::sync(lg);
    Ann_node_cfgmeta::sync(lg);
    Ann_node_file_loc::sync(lg);
  };
};

