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
using Var2ID  = google::dense_hash_map<Char_Array_ID, Index_ID>;

class Symbol_Table {
public:
  Symbol_Table(LGraph *g) : gref(g), pool(gref), lgstrings(g->get_path(), g->get_name() + ST_PREFIX) {
    Type::context = this; // global static pointer for all Types

    symbols.set_empty_key(ST_EMPTY_KEY);
    cache.set_empty_key(VC_EMPTY_KEY);
  }

  void        add(const std::string &var, const Type &type = Type::undefined);
  const Type &get(const std::string &var);
  bool        has(const std::string &var);

  // these throw TypeErrors if the merge fails
  void merge(const std::string &var, const std::string &other);
  void merge(const std::string &var, const Type &other);

  Index_ID find_id(const std::string &var);
  Overflow_Pool *memory_pool() { return &pool; }

private:
  Char_Array_ID str2id(const std::string &);

  LGraph        *gref;
  Overflow_Pool pool;
  Symbols       symbols;
  Var2ID        cache;
  Char_Array<uint16_t> lgstrings;
};

#endif
