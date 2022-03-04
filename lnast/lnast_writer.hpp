//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "lnast.hpp"
#include "lhtree.hpp"

#include <fstream>
#include <ostream>
#include <stack>
#include <fmt/format.h>
#include <fmt/os.h>
#include <fmt/color.h>

class Lnast_writer {
public:
  explicit Lnast_writer(std::ofstream&, std::shared_ptr<Lnast>);
  explicit Lnast_writer(std::ostream&, std::shared_ptr<Lnast>);
  void write_all();
protected:
  int depth;

  bool has_file_output;
  std::ostream &os;
  std::shared_ptr<Lnast> lnast;
  
  std::stack<Lnast_nid> nid_stack;
  Lnast_nid current_nid;

  auto current_text() {
    return lnast->get_data(current_nid).token.get_text();
  }

  bool move_to_child()   {
    nid_stack.push(current_nid);
    current_nid = lnast->get_child(current_nid);
    return !current_nid.is_invalid();
  }

  bool move_to_sibling() {
    current_nid = lnast->get_sibling_next(current_nid);
    return !current_nid.is_invalid();
  }
  
  void move_to_parent() {
    I(nid_stack.size() >= 1);
    current_nid = nid_stack.top();
  }

  auto get_ntype() {
    return lnast->get_type(current_nid).get_raw_ntype();
  }

  bool is_invalid() {
    return current_nid.is_invalid();
  }

  bool is_last_child() {
    return lnast->is_last_child(current_nid);
  }

  template <typename... Args>
  void print(const fmt::text_style& ts, const Args&... args) {
    os << fmt::format(has_file_output ? fmt::text_style() : ts, args...);
  }

  template <typename... Args>
  void print(const Args&... args) {
    os << fmt::format(args...);
  }

  template <typename... Args>
  void print_line(const fmt::text_style& ts, const Args&... args) {
    os << fmt::format("{}", std::string(depth*2, ' '));
    os << fmt::format(has_file_output ? fmt::text_style() : ts, args...);
  }

  template <typename... Args>
  void print_line(const Args&... args) {
    os << fmt::format("{}", std::string(depth*2, ' '));
    os << fmt::format(args...);
  }

  void write_metadata();
  void write_lnast();

  void write_binary(std::string_view op);
  void write_unary(std::string_view op);

  void write_invalid();
  void write_top();
  void write_stmts();  
  void write_if();
  void write_uif();  
  void write_for();
  void write_while();
  void write_func_call();  
  void write_func_def();   
  void write_assign();     
  void write_dp_assign();  
  void write_mut();        
  void write_bit_and();  
  void write_bit_or();   
  void write_bit_not();  
  void write_bit_xor();  
  void write_reduce_or();  
  void write_logical_and();  
  void write_logical_or();   
  void write_logical_not();  
  void write_plus();
  void write_minus();
  void write_mult();
  void write_div();
  void write_mod();
  void write_shl();  
  void write_sra();  
  void write_sext();  
  void write_set_mask();
  void write_get_mask();  
  void write_mask_and();
  void write_mask_popcount();
  void write_mask_xor();
  void write_is();
  void write_ne();
  void write_eq();
  void write_lt();
  void write_le();
  void write_gt();
  void write_ge();
  void write_ref();
  void write_const();
  void write_range();
  void write_tuple_concat();  
  void write_tuple_add();
  void write_tuple_get();
  void write_tuple_set();
  void write_attr_set();
  void write_attr_get();
  void write_err_flag();  
  void write_phi();
  void write_hot_phi();
  void write_last_invalid();
};
