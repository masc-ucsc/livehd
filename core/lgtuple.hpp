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

#include "lconst.hpp"
#include "node.hpp"
#include "node_pin.hpp"

class Lgtuple : std::enable_shared_from_this<Lgtuple> {
private:
protected:
  using Key_map_type = std::vector<std::pair<mmap_lib::str, Node_pin>>;

  const mmap_lib::str name;

  // correct mark as mutable to do not allow tup updates, just to mark the
  // tuple as incorrect (not allow to mark correct once it is incorrect)
  mutable bool           correct;
  static inline Node_pin invalid_dpin;

  mutable Key_map_type key_map;

  void sort_key_map();

  static std::tuple<bool, size_t, size_t> match_int_advance(const mmap_lib::str &a, const mmap_lib::str &b, size_t a_pos, size_t b_pos);
  static std::tuple<bool, bool, size_t>   match_int(const mmap_lib::str &a, const mmap_lib::str &b);

  static mmap_lib::str append_field(const mmap_lib::str &a, const mmap_lib::str &b);
  static std::tuple<mmap_lib::str, mmap_lib::str> learn_fix_int(const mmap_lib::str &a, const mmap_lib::str &b);

  static bool   match(const mmap_lib::str &a, const mmap_lib::str &b);
  static size_t match_first_partial(const mmap_lib::str &a, const mmap_lib::str &b);
  static bool   match_either_partial(const mmap_lib::str &a, const mmap_lib::str &b);

  void        add_int(const mmap_lib::str &key, const std::shared_ptr<Lgtuple const>& tup);
  static void reconnect_flop_if_needed(Node &flop, const mmap_lib::str &flop_name, Node_pin &dpin);

  std::tuple<mmap_lib::str, bool> get_flop_name(const Node &flop) const;

  static bool                      add_pending(Node &node, std::vector<std::pair<mmap_lib::str, Node_pin>> &pending_entries,
                                               const mmap_lib::str &entry_txt, const Node_pin &ubits_dpin, const Node_pin &sbits_dpin);
  static std::pair<Node, Node_pin> flatten_field(Node &result_node, Node_pin &dpin, Node_pin &start_bit_dpin, Node_pin &sbits_dpin,
                                                 Node_pin &ubits_dpin);

public:
  Lgtuple(const mmap_lib::str &_name) : name(_name), correct(true) {}

  static mmap_lib::str get_last_level(const mmap_lib::str &key);
  static mmap_lib::str get_all_but_last_level(const mmap_lib::str &key);
  static mmap_lib::str get_all_but_first_level(const mmap_lib::str &key);

  mmap_lib::str get_name() const { return name; }

  static std::tuple<std::shared_ptr<Lgtuple>, bool> get_mux_tup(const std::vector<std::shared_ptr<Lgtuple const>> &tup_list);
  std::vector<Node::Compact>                        make_mux(Node &mux_node, Node_pin &sel_dpin,
                                                             const std::vector<std::shared_ptr<Lgtuple const>> &tup_list);

  std::tuple<std::shared_ptr<Lgtuple>, bool>      get_flop_tup(Node &flop) const;
  static std::pair<mmap_lib::str,mmap_lib::str>   get_flop_attr_name(const mmap_lib::str &flop_root_name, const mmap_lib::str &cname);
  std::shared_ptr<Lgtuple>                        make_flop(Node &flop) const;

  bool is_correct() const { return correct; }
  void set_issue() const { correct = false; }

  bool has_dpin(const mmap_lib::str &key) const;
  bool has_dpin() const { return has_dpin("0"_str); }

  static std::pair<Port_ID, mmap_lib::str> convert_key_to_io(const mmap_lib::str &key);

  static int              get_first_level_pos(const mmap_lib::str &key);
  static mmap_lib::str get_first_level_name(const mmap_lib::str &key);
  static mmap_lib::str get_canonical_name(const mmap_lib::str &key);
  static int              get_last_level_pos(const mmap_lib::str &key) { return get_first_level_pos(get_last_level(key)); }

  static bool is_single_level(const mmap_lib::str &key) { return key.find('.') == std::string::npos; }

  static bool is_root_attribute(const mmap_lib::str &key) {
    if (key.substr(0, 2) == "__" && key[3] != '_')
      return true;
    if (key.substr(0, 4) == "0.__" && key[5] != '_')
      return true;

    return false;
  }

  static bool is_attribute(const mmap_lib::str &key) {
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

  static mmap_lib::str get_attribute(const mmap_lib::str &key) {
    auto last = get_last_level(key);
    if (last.size() > 2 && last[0] == '_' && last[1] == '_' && last[2] != '_')
      return last;
    return mmap_lib::str();
  }

  mmap_lib::str learn_fix(const mmap_lib::str &key);

  // return const Node_pin ref. WARNING: no pointer stability if add/del fields
  const Node_pin &get_dpin(const mmap_lib::str &key) const;
  const Node_pin &get_dpin() const;

  std::shared_ptr<Lgtuple> get_sub_tuple(const mmap_lib::str &key) const;
  std::shared_ptr<Lgtuple> get_sub_tuple(const std::shared_ptr<Lgtuple const>& tup) const;

  void del(const mmap_lib::str &key);

  // set a dpin/sub tuple. If already existed anything, it is deleted (not attributes)

  void add(const mmap_lib::str &key, const std::shared_ptr<Lgtuple const>& tup);
  void add(const mmap_lib::str &key, const Node_pin &dpin);

  void add(const Node_pin &dpin) {
    // clear everything that is not 0.__attr. Add 0
    return add("0"_str, dpin);
  }

  // TODO: cleaner interface for LNAST and c++ API
  // add_const(mmap_lib::str key, Lconst v);
  // add_const(Lconst v);
  // get_const(mmap_lib::str key) const;

  bool                     concat(const std::shared_ptr<Lgtuple const>& tup2);
  bool                     concat(const Node_pin &dpin);
  Node_pin                 flatten() const;
  std::shared_ptr<Lgtuple> create_assign(const std::shared_ptr<Lgtuple const>& tup) const;
  std::shared_ptr<Lgtuple> create_assign(const Node_pin &dpin) const;

  /// Get all the attributes (__bits) in the same tuple level
  std::vector<std::pair<mmap_lib::str, Node_pin>> get_level_attributes(const mmap_lib::str &key) const;

  const Key_map_type &get_map() const { return key_map; }
  const Key_map_type &get_sort_map() const;

  mmap_lib::str get_scalar_name() const;  // empty if not scalar

  bool is_empty() const { return (key_map.empty()); }

  bool is_scalar() const;
  bool is_ordered() const;
  bool is_trivial_scalar() const;
  bool has_just_attributes() const;

  void dump() const;
};
