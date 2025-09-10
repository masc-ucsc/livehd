//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "lnast.hpp"
#include "lnast_create.hpp"
#include "pass.hpp"
#include "symbol_table.hpp"

class Opt_lnast {
protected:
  Symbol_table st;
  bool         needs_hierarchy;
  bool         hier_mode;
  std::string  top;

  void process_assign(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_plus(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_minus(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_mult(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_div(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_bit_and(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_bit_or(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_bit_not(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_log_and(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_log_or(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_log_not(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_ne(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_eq(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_lt(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_le(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_gt(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_ge(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_if(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_stmts(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_tuple_set(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_tuple_get(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_tuple_add(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);

  void reconstruct_stmts(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid, Lnast_create& ln2);

  void process_todo(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);

  void set_needs_hierarchy();

  void hierarchy_info_int(std::string_view msg);

  template <typename... Args>
  void hierarchy_info(std::format_string<Args...> format, Args&&... args) {
    auto txt(std::format(format, std::forward<Args>(args)...));
    hierarchy_info_int(txt);
  }

public:
  Opt_lnast(const Eprp_var& var);

  void opt(const std::shared_ptr<Lnast>& ln);
  void reconstruct(const std::shared_ptr<Lnast>& ln, Lnast_create& ln2);
};
