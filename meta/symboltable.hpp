#ifndef LGRAPH_SYMBOL_TABLE_H_
#define LGRAPH_SYMBOL_TABLE_H_

#include "lgraphbase.hpp"
#include "integer.hpp"
#include "pyrope_type.hpp"

#include <sparsehash/dense_hash_map>
#include <string>

using Symbol_Mapping = Char_Array<Index_ID>;      // maps Index_ID (of node) => Char_Array_ID (of variable cast from Index_ID),
                                                  //   and Char_Array_ID => variable name
using Type_Mapping   = Dense<Pyrope_Type>;        // Char_Array_ID (of variable name) => Pyrope_Type

const std::string LGRAPH_SYMBOL_TABLE_SYM_FLAG = "_symb";
const std::string LGRAPH_SYMBOL_TABLE_TYP_FLAG = "_varb";

class LGraph_Symbol_Table : virtual public LGraph_Base {
  public:
    LGraph_Symbol_Table() = delete;
    explicit LGraph_Symbol_Table(const std::string &path, const std::string &name) noexcept;
    virtual ~LGraph_Symbol_Table();

    void set_node_variable_type(Index_ID node, const std::string &variable, const Pyrope_Type &t);
    const char *get_node_variable(Index_ID node) const;
    const Pyrope_Type &get_node_variable_type(Index_ID id) const;

    void merge(Index_ID n1, Index_ID n2);
    void merge(Index_ID n, const Pyrope_Type &t);

    Char_Array_ID save_integer(const Integer &);
    Integer load_integer(Char_Array_ID) const;
  
    virtual void clear();
    virtual void reload();
    virtual void sync();
    virtual void emplace_back();

  private:
    Char_Array_ID str2id(const std::string &s) { return (symbols.include(s)) ? symbols.get_id(s) : symbols.create_id(s); }
    Char_Array_ID node2id(Index_ID node) const { return symbols.get_field(std::to_string(node)); }

    Symbol_Mapping symbols;
    Type_Mapping   types;
};

#endif
