//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// TODO: Move this as a callback registration for Graph_Library
// Then all the files are distributed per pass as needed

#include "attribute_node_pin_sview.hpp"
#include "attribute_node_pin_data.hpp"

#include "attribute_node_sview.hpp"
#include "attribute_node_data.hpp"

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
  static constexpr char delay[]     = "delay";
  static constexpr char file_loc[]  = "file_loc";
};

using Ann_node_pin_offset   = Attribute_node_pin_data_type<Ann_name::wirename,  Node_pin_mode::Driver, uint16_t>;
using Ann_node_pin_name     = Attribute_node_pin_sview_type<Ann_name::wirename, Node_pin_mode::Driver, true>;
using Ann_node_pin_bitwidth = Attribute_node_pin_data_type<Ann_name::bitwidth , Node_pin_mode::Driver, Ann_bitwidth>;
using Ann_node_pin_delay    = Attribute_node_pin_data_type<Ann_name::delay    , Node_pin_mode::Driver, float>;

using Ann_node_name       = Attribute_node_sview_type<Ann_name::nodename, Attribute_type::Class   , true>;
using Ann_node_place      = Attribute_node_data_type<Ann_name::nodeplace, Ann_place>;
using Ann_node_cfgmeta    = Attribute_node_sview_type<Ann_name::cfgmeta , Attribute_type::Instance, false>;
using Ann_node_file_loc   = Attribute_node_data_type<Ann_name::file_loc , Ann_file_loc>;

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

