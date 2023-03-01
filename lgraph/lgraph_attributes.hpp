//  this file is distributed under the bsd 3-clause license. see license for details.
#pragma once

#include <cstdint>

#include "absl/container/flat_hash_map.h"
#include "ann_file_loc.hpp"
#include "ann_place.hpp"
#include "cell.hpp"
#include "lgraph_base_core.hpp"
#include "lgraphbase.hpp"
#include "node.hpp"
#include "sub_node.hpp"

// This class contains all the main lgraph attributes. An attribute is a
// property like delay, name that must be kept in a map. All the lgraph maps to
// keep associated data should reside in lgraph_attributes.

class Lgraph_attributes : virtual public Lgraph_Base {
public:
  Lgraph_attributes() = delete;
  explicit Lgraph_attributes(std::string_view path, std::string_view name, Lg_type_id _lgid, Graph_library *_lib) noexcept;

  using Node_down_map = absl::flat_hash_map<Node::Compact_class, Lg_type_id>;
  [[nodiscard]] const Node_down_map &get_down_nodes_map() const { return subid_map; };
  // read only, no ref

  using Down_class_map = absl::flat_hash_map<Lg_id_t, int>;
  [[nodiscard]] const Down_class_map &get_down_class_map() const { return down_class_map; };
  // read only, no ref

  using Node_pin_offset_map = absl::flat_hash_map<Node_pin::Compact_class_driver, Bits_t>;
  [[nodiscard]] const Node_pin_offset_map &get_node_pin_offset_map() const { return node_pin_offset_map; };
  [[nodiscard]] Node_pin_offset_map       *ref_node_pin_offset_map() { return &node_pin_offset_map; };

  using Node_pin_name_map  = absl::flat_hash_map<Node_pin::Compact_class_driver, std::string>;
  using Node_pin_name_rmap = absl::flat_hash_map<std::string, Node_pin::Compact_class_driver>;
  [[nodiscard]] const Node_pin_name_map &get_node_pin_name_map() const { return node_pin_name_map; };
  [[nodiscard]] Node_pin_name_map       *ref_node_pin_name_map() { return &node_pin_name_map; };

  [[nodiscard]] const Node_pin_name_rmap &get_node_pin_name_rmap() const { return node_pin_name_rmap; };
  [[nodiscard]] Node_pin_name_rmap       *ref_node_pin_name_rmap() { return &node_pin_name_rmap; };

  using Node_pin_delay_map = absl::flat_hash_map<Node_pin::Compact_driver, float>;
  [[nodiscard]] const Node_pin_delay_map &get_node_pin_delay_map() const { return node_pin_delay_map; };
  [[nodiscard]] Node_pin_delay_map       *ref_node_pin_delay_map() { return &node_pin_delay_map; };

  using Node_pin_unsigned_map = absl::flat_hash_set<Node_pin::Compact_driver>;
  [[nodiscard]] const Node_pin_unsigned_map &get_node_pin_unsigned_map() const { return node_pin_unsigned_map; };
  [[nodiscard]] Node_pin_unsigned_map       *ref_node_pin_unsigned_map() { return &node_pin_unsigned_map; };

  using Node_name_map = absl::flat_hash_map<Node::Compact_class, std::string>;
  [[nodiscard]] const Node_name_map &get_node_name_map() const { return node_name_map; };
  [[nodiscard]] Node_name_map       *ref_node_name_map() { return &node_name_map; };

  using Node_color_map = absl::flat_hash_map<Node::Compact_class, int>;
  [[nodiscard]] const Node_color_map &get_node_color_map() const { return node_color_map; };
  [[nodiscard]] Node_color_map       *ref_node_color_map() { return &node_color_map; };

  using Node_place_map = absl::flat_hash_map<Node::Compact, Ann_place>;
  [[nodiscard]] const Node_place_map &get_node_place_map() const { return node_place_map; };
  [[nodiscard]] Node_place_map       *ref_node_place_map() { return &node_place_map; };

  using Node_loc_map = absl::flat_hash_map<Node::Compact_class, std::pair<uint64_t, uint64_t>>;  // pos1 and pos2 from LN
  [[nodiscard]] const Node_loc_map &get_node_loc_map() const { return node_loc_map; };
  [[nodiscard]] Node_loc_map       *ref_node_loc_map() { return &node_loc_map; };

  using Node_fname_map = absl::flat_hash_map<Node::Compact_class, std::string>;  // source file name from LN
  [[nodiscard]] const Node_fname_map &get_node_fname_map() const { return node_fname_map; };
  [[nodiscard]] Node_fname_map       *ref_node_fname_map() { return &node_fname_map; };

protected:
  using Node_value_map = absl::flat_hash_map<Node::Compact_class, std::string>;
  using Node_lut_map   = absl::flat_hash_map<Node::Compact_class, std::string>;

  Node_value_map const_map;  // TODO?: bimap to avoid unnecessary constant replication
  Node_down_map  subid_map;
  Down_class_map down_class_map;

  Node_lut_map lut_map;

  Node_pin_offset_map   node_pin_offset_map;
  Node_pin_name_map     node_pin_name_map;
  Node_pin_name_rmap    node_pin_name_rmap;
  Node_pin_delay_map    node_pin_delay_map;
  Node_pin_unsigned_map node_pin_unsigned_map;

  Node_name_map  node_name_map;
  Node_color_map node_color_map;
  Node_place_map node_place_map;
  Node_loc_map   node_loc_map;
  Node_fname_map node_fname_map;

  void clear() override;

  // The type attributes are more involved than other attributes because they
  // require to track up/down tables for speed
  void                   set_type(Index_id nid, const Ntype_op op);
  [[nodiscard]] Ntype_op get_type_op(Index_id nid) const { return node_internal[nid].get_type(); }

  [[nodiscard]] bool is_type_const(Index_id nid) const;

  void                     set_type_sub(Index_id nid, Lg_type_id subgraphid);
  [[nodiscard]] Lg_type_id get_type_sub(Index_id nid) const;

  [[nodiscard]] const Sub_node &get_type_sub_node(Index_id nid) const;
  [[nodiscard]] const Sub_node &get_type_sub_node(std::string_view sub_name) const;
  [[nodiscard]] Sub_node       *ref_type_sub_node(Index_id nid);
  [[nodiscard]] Sub_node       *ref_type_sub_node(std::string_view sub_name);

  void                 set_type_lut(Index_id nid, const Lconst &lutid);
  [[nodiscard]] Lconst get_type_lut(Index_id nid) const;

  void set_type_const(Index_id nid, const Lconst &value);
  void set_type_const(Index_id nid, std::string_view value);
  void set_type_const(Index_id nid, int64_t value);

  // No const because Lconst created
  [[nodiscard]] Lconst get_type_const(Index_id nid) const;

  [[nodiscard]] std::tuple<Lg_type_id, Index_id> go_next_down(Index_id nid) const;
};
