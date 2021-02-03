// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// TODO?: Consider using a immutable map/tree (the cprop keeps making copies)
//  https://github.com/arximboldi/immer
//  https://github.com/dotnwat/persistent-rbtree

#pragma once

#include <strings.h> // strcasecmp

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/btree_map.h"
#include "lconst.hpp"
#include "node.hpp"
#include "node_pin.hpp"

class Lgtuple : std::enable_shared_from_this<Lgtuple> {
private:
protected:

  struct less_key {
    bool operator()(const std::string &a, const std::string &b) const {
      return strcasecmp(a.c_str(), b.c_str()) < 0;
    }
  };

  using key_map_type=absl::btree_map<std::string, Node_pin, less_key>;

  // bimap needed for entries that have both key and pos
  using pos2key_map_type=absl::flat_hash_map<std::string, std::string>;
  using key2pos_map_type=absl::flat_hash_map<std::string, std::string>;

  const std::string name;
  Node_pin invalid_dpin;

  key_map_type key_map;
  pos2key_map_type pos2key_map;
  key2pos_map_type key2pos_map;

  key_map_type::const_iterator get_both_it(int pos, std::string_view key) const {
    I(pos>=0);
    const auto it = key_map.find(std::string{key});
    I(get_it(pos) == it);
    return it;
  }

  key_map_type::const_iterator get_it(int pos) const {
    auto str_pos = std::to_string(pos);
    auto it = key_map.find(str_pos);
    if (it!=key_map.end())
      return it; // case when there was no key, just pos

    auto p2k_it = pos2key_map.find(str_pos);
    if (p2k_it==pos2key_map.end())
      return key_map.end(); // case when key did not exist

    it = key_map.find(p2k_it->second);
    I(it != key_map.end()); // both maps should hit in this case
    return it;
  }

  key_map_type::const_iterator get_it(std::string_view key) const;
  key_map_type::const_iterator get_it(int pos, std::string_view key) const {
    if (pos>=0 && !key.empty())
      return get_both_it(pos, key);

    if (pos>=0)
      return get_it(pos);

    return get_it(key);
  }

  key_map_type::const_iterator get_lower_it(int pos, std::string_view key) const {

    if (pos>=0) {
      auto str_pos = std::to_string(pos);

      key_map_type::const_iterator it = key_map.lower_bound(str_pos);
      if (it!=key_map.end()) {
        return it;
      }
      auto p2k_it = pos2key_map.find(str_pos);
      if (p2k_it!=pos2key_map.end())
        return key_map.lower_bound(p2k_it->second);
    }

    return key_map.lower_bound(std::string{key});
  }

  std::string get_remove_first_level(const std::string &key) const;

  void add_int(const std::string &key, std::shared_ptr<Lgtuple const> tup);
  int get_next_free_pos(const std::string &key) const;

public:
  Lgtuple(std::string_view _name) : name(_name) { }

  std::string_view get_name() const { return name; }

  static std::shared_ptr<Lgtuple> make_merge(Node_pin &sel_dpin, const std::vector<std::shared_ptr<Lgtuple const>> &tup_list);

  std::tuple<Node_pin, std::shared_ptr<Lgtuple>> make_select(Node_pin &sel_dpin) const;

  bool has_dpin(int pos)                       const { return get_it(pos)      != key_map.end(); }
  bool has_dpin(std::string_view key)          const { return get_it(key)      != key_map.end(); }
  bool has_dpin()                              const { return has_dpin("");                      }
  bool has_dpin(int pos, std::string_view key) const { return (pos>=0)? has_dpin(pos) : has_dpin(key); }

  // return pos for key, -1 if not existing
  int get_pos(std::string_view key) const;

  // return const Node_pin ref. WARNING: no pointer stability if add/del fields
  const Node_pin &get_dpin(int pos) const;
  const Node_pin &get_dpin(std::string_view key) const;
  const Node_pin &get_dpin() const { I(is_scalar()); return get_dpin(""); }
  const Node_pin &get_dpin(int pos, std::string_view key) const;

  std::shared_ptr<Lgtuple> get_sub_tuple(int pos, std::string_view key) const;
  std::shared_ptr<Lgtuple> get_sub_tuple(int key) const;
  std::shared_ptr<Lgtuple> get_sub_tuple(std::string_view key) const;

  void del(int pos, std::string_view key);
  void del(int pos);
  void del(std::string_view key);

  // set a dpin/sub tuple. If already existed anything, it is deleted (not attributes)
  void add(int pos, std::string_view key, std::shared_ptr<Lgtuple const> tup);
  void add(int pos, std::string_view key, const Node_pin &dpin);

  void add(int pos, std::shared_ptr<Lgtuple const> tup);
  void add(int pos, const Node_pin &dpin);

  void add(std::string_view key, std::shared_ptr<Lgtuple const> tup);
  void add(std::string_view key, const Node_pin &dpin);

  void add(const Node_pin &dpin);

  bool concat(const std::shared_ptr<Lgtuple const> tup2);

  /// Get all the attributes (__bits) in the same tuple level
  std::vector<std::pair<std::string, Node_pin>> get_level_attributes(int pos, std::string_view key) const;

  const key_map_type &get_map() const { return key_map; }

  static std::string get_last_level(const std::string &key);
  static std::string get_all_but_last_level(const std::string &key);

  bool is_scalar() const {
    if (key_map.empty())
      return true;
    return key_map.find("") != key_map.end();
  }

  void dump() const;
};
