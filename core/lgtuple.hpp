// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

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
  std::string parent_key_name; // empty not set
  int         parent_key_pos; // -1 not set

  bool ordered;
  bool named;

  Node_pin val_dpin; //FIXME->sh: rename to val_dpin

  absl::flat_hash_map<std::string, int> key2pos;
  std::vector<std::shared_ptr<Lgtuple>> pos2tuple;

  void reset() {
    ordered = true;
    named   = true;
    key2pos.clear();
    pos2tuple.clear();
  }

  void   unscalarize_if_needed();
  size_t get_or_create_pos(size_t pos, std::string_view key);
  size_t get_or_create_pos(std::string_view key);
  size_t get_or_create_pos(size_t pos);

public:
  Lgtuple()
    : parent_key_pos   (-1) {
    reset();
  }

  Lgtuple(std::string_view name)
    : parent_key_name  (name)
    , parent_key_pos   (-1) {
    reset();
  }

  // pos -1 -> invalid pos
  Lgtuple(int ppos, std::string_view name)
    : parent_key_name  (name)
    , parent_key_pos   (ppos) {
    reset();
  }

  // pos -1 -> invalid pos
  Lgtuple(int ppos)
    : parent_key_pos   (ppos) {
    reset();
  }

  bool             has_parent_key_name() const { return !parent_key_name.empty(); }
  std::string_view get_parent_key_name() const { return parent_key_name; }

  bool   has_parent_key_pos() const { return parent_key_pos>=0; }
  size_t get_parent_key_pos() const { return parent_key_pos; }

  bool has_key_name(std::string_view key) const {
    auto it = key2pos.find(key);
    return it != key2pos.end();
  }

  bool has_key_pos(size_t key) const {
    return (key == 0 || (ordered && key<pos2tuple.size() && pos2tuple[key]));
  }

  size_t get_key_pos(std::string_view key) const {
    I(has_key_name(key));
    auto it = key2pos.find(key);
    return it->second;
  }

  bool has_key_name(size_t key) const {
    bool raw = has_key_pos(key);
    if (!raw)
      return false;
    auto has_name = pos2tuple[key]->has_parent_key_name();
    GI(has_name, has_key_name(pos2tuple[key]->get_parent_key_name()));

    return has_name;
  }

  std::string_view get_key_name(size_t key) const {
    I(has_key_name(key));
    return pos2tuple[key]->get_parent_key_name();
  }

  std::shared_ptr<Lgtuple> get_tuple(std::string_view key);
  std::shared_ptr<Lgtuple> get_tuple(size_t  key);

  bool set(int pos, std::string_view key, const Node_pin &dpin); // int -> pos<0 invalid

  void set(std::string_view key, std::shared_ptr<Lgtuple> tup);
  void set(std::string_view key, LGraph *lg, const Lconst &constant);
  void set(std::string_view key, const Node_pin &dpin);

  void set(size_t pos, std::shared_ptr<Lgtuple> tup);
  void set(size_t pos, LGraph *lg, const Lconst &constant);
  void set(size_t pos, const Node_pin &dpin);

  size_t add(LGraph *lg, const Lconst &constant);
  size_t add(const Node_pin &dpin);
  bool add(const std::shared_ptr<Lgtuple> tup2);

  bool is_scalar() const { return pos2tuple.empty();  }
  bool is_dpin()   const { return !val_dpin.is_invalid(); }

  Node_pin get_value_dpin(int pos, std::string_view key) const; //get driver dpin of value field
  Node_pin get_value_dpin() const { return val_dpin; };

  bool    is_constant() const;
  Lconst get_constant() const;

  void set(LGraph *lg, const Lconst &constant);
  void set(const Node_pin &dpin);

  void dump() const { dump("  "); }
  void dump(std::string_view indent) const;
};


