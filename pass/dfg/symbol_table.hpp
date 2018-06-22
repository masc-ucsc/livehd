//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef CONTEXT_H_
#define CONTEXT_H_

#include <list>
#include <sparsehash/dense_hash_map>
#include <string>

#include "overflow_pool.hpp"
#include "type.hpp"
#include "lgedge.hpp"

typedef google::dense_hash_map<std::string, Type> Symbols;

const std::string DEFAULT_POOL_DIR = ".";
const std::string DHM_EMPTY_KEY    = "<empty>";
const std::string DHM_DELETED_KEY  = "<deleted>";

class Symbol_Table {
public:
  Symbol_Table() : pool(DEFAULT_POOL_DIR) {
    symbols.set_empty_key(DHM_EMPTY_KEY);
    symbols.set_deleted_key(DHM_DELETED_KEY);

    Type::context = this; // global static pointer for all Types
  }

  void        add(const std::string &var)                   { symbols[var] = Type::undefined; }
  void        add(const std::string &var, const Type &type) { symbols[var] = type; }
  const Type &get(const std::string &var)                   { return symbols[var]; } // can't read google::dense_hash_map from const member
  bool        has(const std::string &var)                   { return symbols.find(var) != symbols.end(); }

  // these throw TypeErrors if the merge fails
  void merge(const std::string &var, const std::string &other) { symbols[var].merge(symbols[other]); }
  void merge(const std::string &var, const Type &other)        { symbols[var].merge(other); }

  Overflow_Pool *memory_pool() { return &pool; }

private:
  Overflow_Pool     pool;
  Symbols           symbols;
};

#endif
