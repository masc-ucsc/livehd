//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef CONTEXT_H_
#define CONTEXT_H_

#include <list>
#include <sparsehash/dense_hash_map>
#include <string>

#include "type.hpp"
#include "lgedge.hpp"
#include "lgraph.hpp"
#include "integer.hpp"

const Index_ID      ST_EMPTY_KEY = 0;
const std::string   ST_PREFIX    = "_symbs";

using Symbols = google::dense_hash_map<Index_ID, Type>;

class Symbol_Table {
public:
  Symbol_Table(LGraph *g) : gref(g), lgstrings(gref->get_path(), gref->get_name() + ST_PREFIX) {
    Type::context = this; // global static pointer for all Types
    symbols.set_empty_key(ST_EMPTY_KEY);
  }

  void        add(const std::string &var, const Type &type = Type::undefined);
  const Type &get(const std::string &var) { return (has(var)) ? symbols[lgstrings.get_field(var)] : Type::undefined; }
  bool        has(const std::string &var) const { return lgstrings.include(var); }

  // these throw TypeErrors if the merge fails
  void merge(const std::string &var, const std::string &other);
  void merge(const std::string &var, const Type &other);

  Char_Array_ID save_integer(const Integer &i) { return lgstrings.create_id(i.hex_string()); }
  Integer load_integer(Char_Array_ID cid) { return Integer(lgstrings.get_char(cid)); }

private:
  LGraph                *gref;
  Symbols               symbols;
  Char_Array<Index_ID>  lgstrings;
};

#endif
