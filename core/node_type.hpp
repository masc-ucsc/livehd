//  this file is distributed under the bsd 3-clause license. see license for details.
#pragma once

#include "absl/container/flat_hash_map.h"

#include "cell.hpp"
#include "lgraph_base_core.hpp"
#include "lgraphbase.hpp"
#include "mmap_bimap.hpp"
#include "node.hpp"
#include "sub_node.hpp"

using Node_down_map  = absl::flat_hash_map<Node::Compact_class, Lg_type_id>;
using Down_class_map = absl::flat_hash_map<Lg_id_t, int>;

class Lgraph_Node_Type : virtual public Lgraph_Base {
protected:
  using Node_value_map = absl::flat_hash_map<Node::Compact_class, mmap_lib::str>;
  using Node_lut_map   = absl::flat_hash_map<Node::Compact_class, mmap_lib::str>;

  Node_value_map const_map;  // bimap to avoid unnecessary constant replication

  Node_down_map  subid_map;
  Down_class_map down_class_map;
  Node_lut_map   lut_map;

  void clear() override;

  void     set_type(Index_id nid, const Ntype_op op);
  [[nodiscard]] Ntype_op get_type_op(Index_id nid) const {
    return node_internal[nid].get_type();
  }

  [[nodiscard]] bool is_type_const(Index_id nid) const;

  void       set_type_sub(Index_id nid, Lg_type_id subgraphid);
  [[nodiscard]] Lg_type_id get_type_sub(Index_id nid) const;

  [[nodiscard]] const Sub_node &get_type_sub_node(Index_id nid) const;
  [[nodiscard]] const Sub_node &get_type_sub_node(const mmap_lib::str &sub_name) const;
  [[nodiscard]] Sub_node *      ref_type_sub_node(Index_id nid);
  [[nodiscard]] Sub_node *      ref_type_sub_node(const mmap_lib::str &sub_name);

  void   set_type_lut(Index_id nid, const Lconst &lutid);
  [[nodiscard]] Lconst get_type_lut(Index_id nid) const;

  void set_type_const(Index_id nid, const Lconst &value);
  void set_type_const(Index_id nid, const mmap_lib::str &value);
  void set_type_const(Index_id nid, int64_t value);

  // No const because Lconst created
  [[nodiscard]] Lconst get_type_const(Index_id nid) const;

  [[nodiscard]] std::tuple<Lg_type_id, Index_id> go_next_down(Index_id nid) const;

public:
  Lgraph_Node_Type() = delete;
  explicit Lgraph_Node_Type(const mmap_lib::str &path, const mmap_lib::str &name, Lg_type_id _lgid, Graph_library *_lib) noexcept;

  [[nodiscard]] const Node_down_map  &get_down_nodes_map() const { return subid_map; };
  [[nodiscard]] const Down_class_map &get_down_class_map() const { return down_class_map; };
};
