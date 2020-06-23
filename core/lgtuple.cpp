//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.


#include "lgraph.hpp"
#include "lgtuple.hpp"

std::shared_ptr<Lgtuple> Lgtuple::get(std::string_view key) {
  auto pos = get_key_pos(key);

  return pos2tuple[pos];
}

std::shared_ptr<Lgtuple> Lgtuple::get(size_t pos) {
  I(has_key_pos(pos));

  return pos2tuple[pos];
}

size_t Lgtuple::get_or_create_pos(std::string_view key) {
  if (is_scalar() && !dpin.is_invalid()) {
    named = false; // first did not have name
    pos2tuple.emplace_back(std::make_shared<Lgtuple>(0)); // unname
    pos2tuple[0]->set(dpin);
    dpin.invalidate();
  }

  auto pos = pos2tuple.size();
  bool new_entry = true;

  if (has_key_name(key)) {
    pos = get_key_pos(key);
    new_entry = false;
  }else{
    if (ordered)
      pos2tuple.emplace_back(std::make_shared<Lgtuple>(pos, key)); // ordered+named
    else
      pos2tuple.emplace_back(std::make_shared<Lgtuple>(key)); // named
  }

  return pos;
}

size_t Lgtuple::get_or_create_pos(size_t pos) {
  if (is_scalar() && !dpin.is_invalid()) {
    named = false; // first did not have name
    // move scalar to entry (it should not be named)
    pos2tuple.emplace_back(std::make_shared<Lgtuple>(0)); // unname
    pos2tuple[0]->set(dpin);
    dpin.invalidate();
  }
  I(pos);
  bool new_entry = false;
  if (pos>pos2tuple.size()) {
    pos2tuple.resize(pos+1);
    new_entry = true;
  }else if (pos == pos2tuple.size()) {
    named = false;
    pos2tuple.emplace_back(std::make_shared<Lgtuple>(pos)); // unname
  }else{
    if (pos2tuple[pos]) {
      named = named && pos2tuple[pos]->has_parent_key_name();
    }else{
      new_entry = true;
    }
  }

  if (new_entry) {
    named          = false;
    ordered        = false;
    pos2tuple[pos] = std::make_shared<Lgtuple>(); // unordered, unnamed
  }

  return pos;
}

void Lgtuple::set(std::string_view key, std::shared_ptr<Lgtuple> tup) {
  I(false); // handle copy of tuple recursively
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
  if (pos == 0 && is_scalar()) {
    named = false;
    set(lg, constant);
    return;
  }

  auto pos2 = get_or_create_pos(pos);
  I(pos == pos2);

  pos2tuple[pos]->set(lg, constant);
}

void Lgtuple::set(size_t pos, const Node_pin &_dpin) {
  if (pos == 0 && is_scalar()) {
    set(_dpin);
    return;
  }
  auto pos2 = get_or_create_pos(pos);
  I(pos == pos2);

  pos2tuple[pos]->set(_dpin);
}

size_t Lgtuple::add(LGraph *lg, const Lconst &constant) {
  auto pos = pos2tuple.size();
  if (pos == 0 && !dpin.is_invalid())
    pos++;

  set(pos, lg, constant);
  return pos;
}

size_t Lgtuple::add(const Node_pin &_dpin) {
  I(!_dpin.is_invalid());
  auto pos = pos2tuple.size();
  if (pos == 0 && !dpin.is_invalid())
    pos++;

  set(pos, _dpin);
  return pos;
}

bool Lgtuple::is_constant() const {
  if (dpin.is_invalid())
    return false;

  return dpin.get_node().is_type_const();
}

Lconst Lgtuple::get_constant() const {
  I(is_constant());

  return dpin.get_node().get_type_const();
}

void Lgtuple::set(LGraph *lg, const Lconst &constant) {
  reset();

  auto node = lg->create_node_const(constant);
  dpin = node.setup_driver_pin();
}

void Lgtuple::set(const Node_pin &_dpin) {
  reset();

  dpin = _dpin;
}
