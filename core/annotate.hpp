//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "attribute_node_pin_sview.hpp"
#include "attribute_node_sview.hpp"

struct Ann_name {
  static constexpr char wirename[] = "wirename";
  static constexpr char nodename[] = "nodename";
};

using Ann_node_pin_name = Attribute_node_pin_sview_type<Ann_name::wirename,Node_pin_mode::Driver,true>;
using Ann_node_name     = Attribute_node_sview_type<Ann_name::nodename,true>;

//using Annotate_node_pin_offset = Attribute_node_pin_data_type<"offset",Node_pin_mode::Both>;

struct Ann_support {
  static void clear(LGraph *lg) {
    Ann_node_pin_name::clear(lg);
    Ann_node_name::clear(lg);
  };
  static void sync(LGraph *lg) {
    Ann_node_pin_name::sync(lg);
    Ann_node_name::sync(lg);
  };
};
