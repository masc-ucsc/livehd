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

  using Key_map_type=std::vector<std::pair<std::string, Node_pin>>;

  const std::string name;
  static inline Node_pin invalid_dpin;

  Key_map_type key_map;

  static std::tuple<bool, size_t, size_t> match_int_advance(std::string_view a, std::string_view b, size_t a_pos, size_t b_pos);
	static std::tuple<bool,bool, size_t>    match_int(std::string_view a, std::string_view b);

	static void append_field(std::string &a, std::string_view b);
	static void learn_fix_int(std::string &a, std::string &b);

  static bool   match(std::string_view a, std::string_view b);
	static size_t match_first_partial(std::string_view a, std::string_view b);
	static bool   match_either_partial(std::string_view a, std::string_view b);

  void add_int(const std::string &key, std::shared_ptr<Lgtuple const> tup);

  static std::string_view get_last_level(std::string_view key);
  static std::string_view get_all_but_last_level(std::string_view key);
	static std::string_view get_all_but_first_level(std::string_view key);

public:
  Lgtuple(std::string_view _name) : name(_name) { }

  std::string_view get_name() const { return name; }

  static std::shared_ptr<Lgtuple> make_mux(Node_pin &sel_dpin, const std::vector<std::shared_ptr<Lgtuple const>> &tup_list);
  std::shared_ptr<Lgtuple> make_flop(Node &flop);

  bool has_dpin(std::string_view key)          const;
  bool has_dpin()                              const { return has_dpin("");       }

	static int              get_first_level_pos(std::string_view key);
	static std::string_view get_first_level_name(std::string_view key);

	static bool is_single_level(std::string_view key) {
		return key.find('.') == std::string::npos;
	}

	static bool is_root_attribute(std::string_view key) {
		return (key.substr(0,2) == "__" && key[3] != '_');
	}

	static bool is_attribute(std::string_view key) {
		if (is_root_attribute(key))
			return true;

		auto it = key.find(".__");
		if (it != std::string::npos) {
			if (key[it+3] != '_')
				return true;
		}

		auto it2 = key.find(":__");
		if (it2 != std::string::npos) {
			if (key[it2+3] != '_')
				return true;
		}

		return false;
	}


	std::string learn_fix(std::string_view key);

  // return const Node_pin ref. WARNING: no pointer stability if add/del fields
  const Node_pin &get_dpin(std::string_view key) const;
  const Node_pin &get_dpin() const { I(is_scalar()); return get_dpin(""); }

  std::shared_ptr<Lgtuple> get_sub_tuple(std::string_view key) const;

  void del(std::string_view key);

  // set a dpin/sub tuple. If already existed anything, it is deleted (not attributes)

  void add(std::string_view key, std::shared_ptr<Lgtuple const> tup);
  void add(std::string_view key, const Node_pin &dpin);

  void add(const Node_pin &dpin) {
		return add("", dpin);
	}

  bool append_tuple(const std::shared_ptr<Lgtuple const> tup2);
  bool append_tuple(const Node_pin &dpin);

  /// Get all the attributes (__bits) in the same tuple level
  std::vector<std::pair<std::string, Node_pin>> get_level_attributes(std::string_view key) const;

  const Key_map_type &get_map() const { return key_map; }

  bool is_scalar() const {
    if (key_map.empty())
      return true;
    return key_map.size() == 1 && key_map[0].first == "";
  }

  void dump() const;
};
