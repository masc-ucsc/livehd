//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <string_view>
#include <tuple>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "lgraph_base_core.hpp"
#include "lhtree.hpp"

class Node;
class Lgraph;

// -1: non hierarchical or invalid hidx
//  0: root hidx
//   : a child

class Hierarchy {
protected:
  Lgraph *top;

  struct up_entry_t {
    Lgraph             *parent_lg;
    Index_id            parent_nid;
    Hierarchy_index     parent_hidx;
    mutable std::string name;  // lazy populated (only on get_name)
  };

  struct key_entry_t {
    Index_id        parent_nid;
    Hierarchy_index parent_hidx;

    key_entry_t(Index_id a, Hierarchy_index b) : parent_nid(a), parent_hidx(b) {}

    template <typename H>
    friend H AbslHashValue(H h, const key_entry_t &s) {
      return H::combine(std::move(h), s.parent_nid.value, s.parent_hidx);
    };
    constexpr bool operator==(const key_entry_t &other) const {
      return parent_nid == other.parent_nid && parent_hidx == other.parent_hidx;
    }
  };

  std::vector<up_entry_t>                           up_vector;
  absl::flat_hash_map<key_entry_t, Hierarchy_index> down_map;

  [[nodiscard]] std::tuple<Hierarchy_index, Lgraph *, Index_id> get_instance_up(const Hierarchy_index hidx) const;

public:
  Hierarchy(Lgraph *top);

  void clear();

  [[nodiscard]] Lgraph *ref_lgraph(const Hierarchy_index hidx) const;

  [[nodiscard]] std::string_view get_name(const Hierarchy_index hidx) const;

  [[nodiscard]] Node get_instance_up_node(const Hierarchy_index hidx) const;

  [[nodiscard]] Hierarchy_index add_go_down(Hierarchy_index parent_hidx, Lgraph *parent_lg, Index_id nid);
  [[nodiscard]] Hierarchy_index go_up(const Hierarchy_index hidx) const;
  [[nodiscard]] Hierarchy_index go_down(const Node &node);
  [[nodiscard]] Hierarchy_index go_up(const Node &node) const;

  [[nodiscard]] std::tuple<Hierarchy_index, Lgraph *> get_next(const Hierarchy_index hidx) const;

  static constexpr Hierarchy_index hierarchical_root() { return 0; }
  static constexpr Hierarchy_index non_hierarchical() { return -1; }

  static bool           is_root(const Node &node);
  static constexpr bool is_root(const Hierarchy_index hidx) { return hidx == 0; }
  static constexpr bool is_invalid(const Hierarchy_index hidx) { return hidx == -1; }

  [[nodiscard]] size_t size() const { return up_vector.size(); }

  void dump() const;
};
