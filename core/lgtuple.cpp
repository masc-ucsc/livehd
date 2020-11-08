//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgtuple.hpp"

#include "lgraph.hpp"

Node_pin Lgtuple::get_value_dpin(int pos, std::string_view key) const {
  if (pos == 0 && is_scalar()) {
    return val_dpin;
  }
  if (pos < 0)
    pos = get_key_pos(key);

  I(pos < pos2tuple.size());
  return pos2tuple[pos]->get_value_dpin();
}

std::shared_ptr<Lgtuple> Lgtuple::get_tuple(std::string_view key) {
  auto pos = get_key_pos(key);

  return pos2tuple[pos];
}

std::shared_ptr<Lgtuple> Lgtuple::get_tuple(size_t pos) {
  I(has_key_pos(pos));

  if (pos == 0 && is_scalar())
    return shared_from_this();

  return pos2tuple[pos];
}

std::shared_ptr<Lgtuple> Lgtuple::get_tuple(int pos, std::string_view key) {
  if (pos == 0 && is_scalar()) {
    return shared_from_this();
  }
  if (pos < 0) {
    if (!has_key_name(key))
      return nullptr; // field not found

    pos = get_key_pos(key);
  } else if (has_key_name(key)) {
    if (static_cast<size_t>(pos)!=get_key_pos(key))
      return nullptr; // Should be a compile error. Inconsistent field
  }

  I(pos>=0);
  I(static_cast<size_t>(pos) < pos2tuple.size());
  return pos2tuple[pos];
}

void Lgtuple::unscalarize_if_needed() {
  if (is_scalar() && !val_dpin.is_invalid()) {
    named = false;                                         // first did not have name
    pos2tuple.emplace_back(std::make_shared<Lgtuple>(0));  // unname
    pos2tuple[0]->set(val_dpin);
    val_dpin.invalidate();
  }
}

size_t Lgtuple::get_or_create_pos(size_t pos, std::string_view key) {
  unscalarize_if_needed();

  if (pos > pos2tuple.size()) {
    pos2tuple.resize(pos + 1);
    pos2tuple[pos] = std::make_shared<Lgtuple>(pos, key);
    key2pos[key]   = pos;
  } else if (pos == pos2tuple.size()) {
    pos2tuple.emplace_back(std::make_shared<Lgtuple>(pos, key));
    key2pos[key] = pos;
  } else {
    if (pos2tuple[pos]) {
      I(pos2tuple[pos]->get_hier_parent_key_name() == key);
    } else {
      pos2tuple[pos] = std::make_shared<Lgtuple>(pos, key);
      key2pos[key]   = pos;
    }
  }

  return pos;
}

size_t Lgtuple::get_or_create_pos(std::string_view key) {
  unscalarize_if_needed();

  size_t pos;
  if (has_key_name(key)) {
    pos       = get_key_pos(key);
  } else {
    pos       = pos2tuple.size();
    pos2tuple.emplace_back(std::make_shared<Lgtuple>(key));  // named
    key2pos[key] = pos;
    ordered = false;
  }

  return pos;
}

size_t Lgtuple::get_or_create_pos(size_t pos) {
  unscalarize_if_needed();

  named = false;
  I(pos);
  bool new_entry = false;
  if (pos > pos2tuple.size()) {
    pos2tuple.resize(pos + 1);
    new_entry = true;
  } else if (pos == pos2tuple.size()) {
    pos2tuple.emplace_back(std::make_shared<Lgtuple>(pos));  // unname
  } else {
    if (pos2tuple[pos]) {
      named = named && pos2tuple[pos]->has_hier_parent_key_name();
    } else {
      new_entry = true;
    }
  }

  if (new_entry) {
    named          = false;
    pos2tuple[pos] = std::make_shared<Lgtuple>();  // unordered, unnamed
  }

  return pos;
}

bool Lgtuple::set(int pos, std::string_view key, const Node_pin &_val_dpin) {
  if (!key.empty() && pos >= 0) {
    auto it = key2pos.find(key);
    if (it != key2pos.end() && it->second != pos)
      return false;
  }

  if (pos < 0) {
    set(key, _val_dpin);
  } else if (key.empty()) {
    set(pos, _val_dpin);
  } else {
    auto pos2 = get_or_create_pos(pos, key);
    pos2tuple[pos2]->set(_val_dpin);
  }

  return true;
}

void Lgtuple::set(std::string_view key, std::shared_ptr<Lgtuple> tup2) {
  auto it = key2pos.find(key);
  if (it == key2pos.end()) {
    auto shift = pos2tuple.size();
    tup2->hier_parent_key_name = key; 
    pos2tuple.emplace_back(tup2);
    /* key2pos[key] = shift; */
    key2pos.insert_or_assign(key, shift);
  } else {
    I(pos2tuple.size() > it->second);
    I(!(pos2tuple[it->second]->is_valid_val_dpin() && tup2->is_valid_val_dpin())); // Which one to pick??!!??
    if (tup2->is_valid_val_dpin()) {
      pos2tuple[it->second]->set(tup2->get_value_dpin()); // also resets non attr fields
    } else {
      pos2tuple[it->second]->dump();
      pos2tuple[it->second]->reset_non_attr_fields();
    }
    pos2tuple[it->second]->add(tup2);
  }

  if (tup2->hier_parent_key_name.empty())
    tup2->hier_parent_key_name = key;
  else if (tup2->hier_parent_key_name[0] == '_' && tup2->hier_parent_key_name[2] == '_')
    tup2->hier_parent_key_name = key;
}

void Lgtuple::set(std::string_view key, LGraph *lg, const Lconst &constant) {
  auto pos = get_or_create_pos(key);

  pos2tuple[pos]->set(lg, constant);
}

void Lgtuple::set(std::string_view key, const Node_pin &_dpin) {
  auto pos = get_or_create_pos(key);

  pos2tuple[pos]->set(_dpin);
}

void Lgtuple::set(size_t pos, LGraph *lg, const Lconst &constant) {
  named = false;
  if (pos == 0 && is_scalar()) {
    set(lg, constant);
    return;
  }

  auto pos2 = get_or_create_pos(pos);
  I(pos == pos2);

  pos2tuple[pos]->set(lg, constant);
}

void Lgtuple::set(size_t pos, const Node_pin &_val_dpin) {
  named = false;
  if (pos == 0 && is_scalar()) {
    set(_val_dpin);
    return;
  }
  auto pos2 = get_or_create_pos(pos);
  I(pos == pos2);

  pos2tuple[pos]->set(_val_dpin);
}

size_t Lgtuple::add(LGraph *lg, const Lconst &constant) {
  auto pos = pos2tuple.size();
  if (pos == 0 && !val_dpin.is_invalid())
    pos++;

  set(pos, lg, constant);
  return pos;
}

size_t Lgtuple::add(const Node_pin &_val_dpin) {
  I(!_val_dpin.is_invalid());
  auto pos = pos2tuple.size();
  if (pos == 0 && !val_dpin.is_invalid())
    pos++;

  set(pos, _val_dpin);
  return pos;
}

// for tuple concatenate
bool Lgtuple::add(const std::shared_ptr<Lgtuple> tup2) {
  // check label overlap
  for (auto e : tup2->key2pos) {
    if (key2pos.count(e.first)) {
      return false;      }
  }

  named   = named && tup2->named;
  ordered = ordered && tup2->ordered;

  if (tup2->is_scalar()) {
    add(val_dpin);
  } else {
    auto shift = pos2tuple.size();
    for (auto i = 0u; i < tup2->pos2tuple.size(); ++i) {
      pos2tuple.emplace_back(tup2->pos2tuple[i]);
    }
    for (auto e : tup2->key2pos) {
      key2pos[e.first] = e.second + shift;
    }
  }
  return true;
}

bool Lgtuple::is_constant() const {
  if (val_dpin.is_invalid())
    return false;

  return val_dpin.get_node().is_type_const();
}

Lconst Lgtuple::get_constant() const {
  I(is_constant());

  return val_dpin.get_node().get_type_const();
}

void Lgtuple::reset_non_attr_fields() {
  ordered = true;
  named   = true;
  for(auto it=key2pos.begin();it!=key2pos.end();) {
    if (it->first.substr(0,2) == "__") {
      it ++;
      continue;
    }
    pos2tuple[it->second] = nullptr;
    key2pos.erase(it++);
  }
}

void Lgtuple::set(LGraph *lg, const Lconst &constant) {
  reset_non_attr_fields();

  auto node = lg->create_node_const(constant);
  val_dpin  = node.setup_driver_pin();
}

void Lgtuple::set(const Node_pin &_val_dpin) {
  reset_non_attr_fields();

  val_dpin = _val_dpin;  // this means the val_dpin of final TupAdd node from the most-up-to-date tuple-chain
}

std::vector<std::pair<std::string_view, Node_pin>> Lgtuple::get_all_attributes() const {

  std::vector<std::pair<std::string_view, Node_pin>> v;

  for(auto it=key2pos.begin(); it!=key2pos.end(); ++it) {
    if (it->first.substr(0,2) != "__")
      continue;

    v.emplace_back(it->first, pos2tuple[it->second]->get_value_dpin());
  }

  return v;
}

void Lgtuple::dump(std::string_view indent) const {
  fmt::print("{}hier_parent_key_name:{} hier_parent_key_pos:{} {} {} {} val_dpin:{}\n",
             indent,
             hier_parent_key_name,
             hier_parent_key_pos,
             ordered ? "ordered" : "unordered",
             named ? "named" : "unnamed",
             is_scalar() ? "scalar" : "not-scalar",
             val_dpin.debug_name());

  std::string indent2(indent);
  indent2.append(2, ' ');
  for (auto i = 0u; i < pos2tuple.size(); ++i) {
    if (pos2tuple[i])
      pos2tuple[i]->dump(indent2);
    else
      fmt::print("{}invalid pos:{}\n", indent2, i);
  }
}

void Lgtuple::analyze_graph_output(absl::flat_hash_map<std::string, Node_pin> &gout2driver, std::string base_name) const {
  std::string new_hier_name;
  if (hier_parent_key_name != "%") {


    if (hier_parent_key_name[0] == '%') {
      new_hier_name = hier_parent_key_name.substr(1);
    } else {
      new_hier_name = absl::StrCat(base_name, ".", hier_parent_key_name);
    }

    if (is_valid_val_dpin()) {
      gout2driver.insert_or_assign(new_hier_name, val_dpin);
      return;
    }
  }

  for (auto i = 0u; i < pos2tuple.size(); ++i) {
    if (pos2tuple[i])
      pos2tuple[i]->analyze_graph_output(gout2driver, new_hier_name);
    else
      fmt::print("{}invalid pos:{}\n", "  ", i);
  }
}
