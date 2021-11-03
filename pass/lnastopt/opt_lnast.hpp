//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "lnast.hpp"
#include "lnast_create.hpp"
#include "pass.hpp"
#include "symbol_table.hpp"

class Opt_lnast {
protected:
  Symbol_table  st;
  bool          needs_hierarchy;
  bool          hier_mode;
  mmap_lib::str top;

  void process_assign(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_plus(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_minus(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_bit_or(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_stmts(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_tuple_set(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_tuple_get(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);
  void process_tuple_add(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);

  void reconstruct_stmts(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid, Lnast_create& ln2);

  void process_todo(const std::shared_ptr<Lnast>& ln, const Lnast_nid& lnid);

  void set_needs_hierarchy();

  void hierarchy_info_int(const std::string& msg);

  template <typename S, typename... Args>
  void hierarchy_info(const S& format, Args&&... args) {
    std::string txt(fmt::format(format, args...));
    hierarchy_info_int(txt);
  }

public:
  Opt_lnast(const Eprp_var& var);

  void opt(const std::shared_ptr<Lnast>& ln);
  void reconstruct(const std::shared_ptr<Lnast>& ln, Lnast_create& ln2);
};
