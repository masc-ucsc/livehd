#ifndef CONTEXT_H_
#define CONTEXT_H_

#include <list>
#include <sparsehash/dense_hash_map>
#include <string>

#include "Overflow_Pool.hpp"
#include "Type.hpp"

namespace Pyrope {
typedef std::string                                       VarID;
typedef google::dense_hash_map<VarID, Type>               SymbolTable;
typedef google::dense_hash_map<VarID, std::vector<VarID>> DictionaryMembers;

const VarID DEFAULT_POOL_DIR = ".";
const VarID DHM_EMPTY_KEY    = "<empty>";
const VarID DHM_DELETED_KEY  = "<deleted>";

class Context {
public:
  Context() : pool(DEFAULT_POOL_DIR) {
    symbols.set_empty_key(DHM_EMPTY_KEY);
    symbols.set_deleted_key(DHM_DELETED_KEY);

    dict_members.set_empty_key(DHM_EMPTY_KEY);
    dict_members.set_deleted_key(DHM_DELETED_KEY);

    Type::context = this; // global static pointer for all Types
  }

  void        add(const VarID &var, const Type &type) { symbols[var] = type; }
  const Type &get(const VarID &var) { return symbols[var]; } // can't read google::dense_hash_map from const member

  const std::vector<VarID> &get_members(const VarID &dict) { return dict_members[dict]; }

  // these throw TypeErrors if the merge fails
  void merge(const VarID &var, const VarID &other) { symbols[var].merge(symbols[other]); }
  void merge(const VarID &var, const Type &other) { symbols[var].merge(other); }

  Overflow_Pool *memory_pool() { return &pool; }

private:
  Overflow_Pool     pool;
  SymbolTable       symbols;
  DictionaryMembers dict_members;
};
} // namespace Pyrope

#endif
