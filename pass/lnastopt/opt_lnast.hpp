//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "symbol_table.hpp"
#include "lnast.hpp"
#include "pass.hpp"

class Opt_lnast {
protected:
  Symbol_table st;

  void process_plus     (const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_tuple_add(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_assign   (const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_todo     (const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);

public:
  Opt_lnast(const Eprp_var& var);

  void opt(const std::shared_ptr<Lnast>& ln);
};

