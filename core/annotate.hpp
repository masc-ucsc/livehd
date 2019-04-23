//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "attribute_node_pin_sview.hpp"
#include "attribute_node_pin_data.hpp"

#include "attribute_node_sview.hpp"
#include "attribute_node_data.hpp"

#include "node_place.hpp"

struct Ann_name {
  static constexpr char offset[]    = "offset";
  static constexpr char wirename[]  = "wirename";
  static constexpr char nodename[]  = "nodename";
  static constexpr char nodeplace[] = "nodeplace";
  static constexpr char cfgmeta[]   = "cfgmeta";
};

using Ann_node_pin_offset = Attribute_node_pin_data_type<Ann_name::wirename,  Node_pin_mode::Driver, uint16_t>;
using Ann_node_pin_name   = Attribute_node_pin_sview_type<Ann_name::wirename, Node_pin_mode::Driver, false>;

using Ann_node_name       = Attribute_node_sview_type<Ann_name::nodename, true>;
using Ann_node_place      = Attribute_node_data_type<Ann_name::nodeplace, Node_place>;
using Ann_node_cfgmeta    = Attribute_node_sview_type<Ann_name::cfgmeta , false>;

struct Ann_support {
  static void clear(LGraph *lg) {
    Ann_node_pin_offset::clear(lg);
    Ann_node_pin_name::clear(lg);

    Ann_node_name::clear(lg);
    Ann_node_place::clear(lg);
    Ann_node_cfgmeta::clear(lg);
  };

  static void sync(LGraph *lg) {
    Ann_node_pin_offset::sync(lg);
    Ann_node_pin_name::sync(lg);

    Ann_node_name::sync(lg);
    Ann_node_place::sync(lg);
    Ann_node_cfgmeta::sync(lg);
  };
};

