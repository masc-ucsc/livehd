// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// TODO?: Consider using a immutable map/tree (the cprop keeps making copies)
//  https://github.com/arximboldi/immer
//  https://github.com/dotnwat/persistent-rbtree

#pragma once

#include <strings.h>  // strcasecmp

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "lconst.hpp"
#include "node.hpp"
#include "node_pin.hpp"

class Lgtuple : std::enable_shared_from_this<Lgtuple> {
private:
protected:
  using Key_map_type = std::vector<std::pair<std::string, Node_pin>>;

  const std::string name;

  // correct mark as mutable to do not allow tup updates, just to mark the
  // tuple as incorrect (not allow to mark correct once it is incorrect)
  mutable bool           correct;
  static inline Node_pin invalid_dpin;

  mutable Key_map_type key_map;

  void sort_key_map();

  static std::tuple<bool, size_t, size_t> match_int_advance(std::string_view a, std::string_view b, size_t a_pos, size_t b_pos);
  static std::tuple<bool, bool, size_t>   match_int(std::string_view a, std::string_view b);

  static void append_field(std::string &a, std::string_view b);
  static void learn_fix_int(std::string &a, std::string &b);

  static bool   match(std::string_view a, std::string_view b);
  static size_t match_first_partial(std::string_view a, std::string_view b);
  static bool   match_either_partial(std::string_view a, std::string_view b);

  void        add_int(const std::string &key, std::shared_ptr<Lgtuple const> tup);
  static void reconnect_flop_if_needed(Node &flop, const std::string &flop_name, Node_pin &dpin);

  std::tuple<std::string_view, bool> get_flop_name(const Node &flop) const;

public:
  Lgtuple(std::string_view _name) : name(_name), correct(true) {}

  static std::string_view get_last_level(std::string_view key);
  static std::string_view get_all_but_last_level(std::string_view key);
  static std::string_view get_all_but_first_level(std::string_view key);

  std::string_view get_name() const { return name; }

  static std::tuple<std::shared_ptr<Lgtuple>, bool> get_mux_tup(const std::vector<std::shared_ptr<Lgtuple const>> &tup_list);
  std::vector<Node::Compact>                        make_mux(Node &mux_node, Node_pin &sel_dpin,
                                                             const std::vector<std::shared_ptr<Lgtuple const>> &tup_list);

  std::tuple<std::shared_ptr<Lgtuple>, bool> get_flop_tup(Node &flop) const;
  std::shared_ptr<Lgtuple>                   make_flop(Node &flop) const;

  bool is_correct() const { return correct; }
  void set_issue() const { correct = false; }

  bool has_dpin(std::string_view key) const;
  bool has_dpin() const { return has_dpin(""); }

  static std::pair<Port_ID, std::string_view> convert_key_to_io(std::string_view key);

  static int              get_first_level_pos(std::string_view key);
  static std::string_view get_first_level_name(std::string_view key);
  static std::string_view get_canonical_name(std::string_view key);
  static int              get_last_level_pos(std::string_view key) { return get_first_level_pos(get_last_level(key)); }

  static bool is_single_level(std::string_view key) { return key.find('.') == std::string::npos; }

  static bool is_root_attribute(std::string_view key) {
    if (key.substr(0, 2) == "__" && key[3] != '_')
      return true;
//    if (key.substr(0, 4) == "0.__" && key[5] != '_')
//      return true;

    return false;
  }

  static bool is_attribute(std::string_view key) {
    if (is_root_attribute(key))
      return true;

    auto it = key.find(".__");
    if (it != std::string::npos) {
      if (key[it + 3] != '_')
        return true;
    }

    auto it2 = key.find(":__");
    if (it2 != std::string::npos) {
      if (key[it2 + 3] != '_')
        return true;
    }

    return false;
  }

  static std::string_view get_attribute(std::string_view key) {
    if (is_root_attribute(key))
      return key;

    auto it = key.find(".__");
    if (it != std::string::npos) {
      if (key[it + 3] != '_')
        return key.substr(it+1);
    }

    auto it2 = key.find(":__");
    if (it2 != std::string::npos) {
      if (key[it2 + 3] != '_')
        return key.substr(it2+1);
    }

    return "";
  }

  std::string learn_fix(std::string_view key);

  // return const Node_pin ref. WARNING: no pointer stability if add/del fields
  const Node_pin &get_dpin(std::string_view key) const;
  const Node_pin &get_dpin() const {
    I(is_scalar());
    return get_dpin("");
  }

  std::shared_ptr<Lgtuple> get_sub_tuple(std::string_view key) const;
  std::shared_ptr<Lgtuple> get_sub_tuple(std::shared_ptr<Lgtuple const> tup) const;

  void del(std::string_view key);

  // set a dpin/sub tuple. If already existed anything, it is deleted (not attributes)

  void add(std::string_view key, std::shared_ptr<Lgtuple const> tup);
  void add(std::string_view key, const Node_pin &dpin);

  void add(const Node_pin &dpin) { return add("", dpin); }

  bool     concat(const std::shared_ptr<Lgtuple const> tup2);
  bool     concat(const Node_pin &dpin);
  Node_pin flatten() const;

  /// Get all the attributes (__bits) in the same tuple level
  std::vector<std::pair<std::string, Node_pin>> get_level_attributes(std::string_view key) const;

  const Key_map_type &get_map() const { return key_map; }
  const Key_map_type &get_sort_map() const;

  std::string get_scalar_name() const;  // empty if not scalar

  bool is_empty() const { return (key_map.empty()); }

  bool is_scalar() const;
  bool is_ordered() const;
  bool is_trivial_scalar() const;
  bool has_just_attributes() const;

  void dump() const;
};
