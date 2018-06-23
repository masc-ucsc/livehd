//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef CONTEXT_H_
#define CONTEXT_H_

#include <list>
#include <sparsehash/dense_hash_map>
#include <string>

#include "overflow_pool.hpp"
#include "type.hpp"
#include "lgedge.hpp"
#include "lgraph.hpp"

const Index_ID      ST_EMPTY_KEY = 0;
const Char_Array_ID VC_EMPTY_KEY = 0;
const std::string   ST_PREFIX    = "_symbs";

using Symbols = google::dense_hash_map<Index_ID, Type>;

class Symbol_Table {
public:
  Symbol_Table(LGraph *g) : gref(g), pool(gref), lgstrings(gref->get_path(), gref->get_name() + ST_PREFIX) {
    Type::context = this; // global static pointer for all Types
    symbols.set_empty_key(ST_EMPTY_KEY);
  }

  void        add(const std::string &var, const Type &type = Type::undefined);
  const Type &get(const std::string &var) { return (lgstrings.include(var)) ? symbols[lgstrings.get_field(var)] : Type::undefined; }
  bool        has(const std::string &var) const { return lgstrings.include(var); }

  // these throw TypeErrors if the merge fails
  void merge(const std::string &var, const std::string &other);
  void merge(const std::string &var, const Type &other);

  Overflow_Pool *memory_pool() { return &pool; }

private:

  LGraph                *gref;
  Overflow_Pool         pool;
  Symbols               symbols;
  Char_Array<Index_ID>  lgstrings;
};

#endif
