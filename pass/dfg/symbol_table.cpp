//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "symbol_table.hpp"

void Symbol_Table::add(const std::string &var, const Type &type) {
  Index_ID vid = find_id(var);

  if (vid == ST_EMPTY_KEY)
    vid = gref->create_node().get_nid();
  
  symbols[vid] = type;
}

const Type &Symbol_Table::get(const std::string &var) {
  Index_ID vid = find_id(var);
  return (vid != ST_EMPTY_KEY) ? symbols[vid] : Type::undefined;
}

bool Symbol_Table::has(const std::string &var) { return find_id(var) != ST_EMPTY_KEY; }

Index_ID Symbol_Table::find_id(const std::string &var) {
  if (lgstrings.include(var)) {
    Char_Array_ID cid = lgstrings.get_id(var.c_str());
  
    if (cache[cid] != 0)
      return cache[cid];
  }

  return ST_EMPTY_KEY;
}

void Symbol_Table::merge(const std::string &var, const std::string &other) {
  Index_ID oid = find_id(other);
  assert(oid != ST_EMPTY_KEY);

  merge(var, symbols[oid]);
}

void Symbol_Table::merge(const std::string &var, const Type &other) {
  Index_ID vid = find_id(var);
  assert(vid != ST_EMPTY_KEY);

  symbols[vid].merge(other);
}

Char_Array_ID Symbol_Table::str2id(const std::string &var) {
  return (lgstrings.include(var)) ? lgstrings.get_id(var) : lgstrings.create_id(var);
}
