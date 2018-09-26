#include "symboltable.hpp"

LGraph_Symbol_Table::LGraph_Symbol_Table(const std::string &path, const std::string &name) noexcept
  : Lgraph_base_core(path, name), LGraph_Base(path, name)
  , symbols(path + "/lgraph_" + name + LGRAPH_SYMBOL_TABLE_SYM_FLAG)
  , types(path + "/lgraph_" + name + LGRAPH_SYMBOL_TABLE_TYP_FLAG) { }

LGraph_Symbol_Table::~LGraph_Symbol_Table() {
  symbols.sync();
  types.sync();
}

void LGraph_Symbol_Table::merge(Index_ID n1, Index_ID n2) {
  merge(n1, get_node_variable_type(n2));
}

void LGraph_Symbol_Table::merge(Index_ID n, const Pyrope_Type &t) {
  types[node2id(n)].merge(this, t);
}

Char_Array_ID LGraph_Symbol_Table::save_integer(const Integer &i) {
  auto istr = i.hex_string();
  return symbols.create_id(istr);
}

Integer LGraph_Symbol_Table::load_integer(Char_Array_ID cid) const {
  auto istr = symbols.get_char(cid);
  return Integer(istr);
}

void LGraph_Symbol_Table::set_node_variable_type(Index_ID node, const std::string &variable, const Pyrope_Type &t) {
  Char_Array_ID cid = str2id(variable);
  auto str_node = std::to_string(node);

  symbols.create_id(str_node, cid);
  types[cid] = t;
}

const Pyrope_Type &LGraph_Symbol_Table::get_node_variable_type(Index_ID node) const {
  return types[node2id(node)];
}

const char *LGraph_Symbol_Table::get_node_variable(Index_ID node) const {
  return symbols.get_char(node2id(node));
}

void LGraph_Symbol_Table::clear() {
  symbols.clear();
  types.clear();
}

void LGraph_Symbol_Table::reload() {
  symbols.reload();
  types.reload();
}

void LGraph_Symbol_Table::sync() {
  symbols.sync();
  types.sync();
}

void LGraph_Symbol_Table::emplace_back() {
  types.emplace_back();
}