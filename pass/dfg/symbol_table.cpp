//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "symbol_table.hpp"

void Symbol_Table::add(const std::string &var, const Type &type) {
  if (lgstrings.include(var)) {
    Index_ID symb_id = lgstrings.get_field(var.c_str());
    symbols[symb_id] = type;
  } else {
    Index_ID symb_id = gref->create_node().get_nid();
    lgstrings.create_id(var, symb_id);

    symbols[symb_id] = type;
  }
}

void Symbol_Table::merge(const std::string &var, const std::string &other) {
  assert(has(other));
  merge(var, symbols[lgstrings.get_field(other)]);
}

void Symbol_Table::merge(const std::string &var, const Type &other) {
  assert(has(var));
  symbols[lgstrings.get_field(var)].merge(other);
}
