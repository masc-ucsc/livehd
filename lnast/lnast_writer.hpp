//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <fmt/color.h>
#include <fmt/format.h>
#include <fmt/os.h>

#include <fstream>
#include <ostream>
#include <stack>

#include "lhtree.hpp"
#include "lnast.hpp"

class Lnast_writer {
public:
  explicit Lnast_writer(std::ostream&, const std::shared_ptr<Lnast>&);
  void write_all();
  void write_nid(const Lnast_nid& nid);

protected:
  int  depth;
  bool is_func_name;

  bool                          has_file_output;
  std::ostream&                 os;
  const std::shared_ptr<Lnast>& lnast;

  std::stack<Lnast_nid> nid_stack;
  Lnast_nid             current_nid;

  auto current_text() { return lnast->get_data(current_nid).token.get_text(); }

  bool move_to_child() {
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
    nid_stack.pop();
  }

  auto get_ntype() { return lnast->get_type(current_nid); }

  auto get_raw_ntype() { return lnast->get_type(current_nid).get_raw_ntype(); }

  bool is_invalid() { return current_nid.is_invalid(); }

  bool is_last_child() { return lnast->is_last_child(current_nid); }

  template <typename... Args>
  void print(const fmt::text_style& ts, const Args&... args) {
    if (has_file_output) {
      os << fmt::format("{}", args...);
    } else {
      os << fmt::format(ts, "{}", args...);
    }
  }

  template <typename... Args>
  void print(fmt::format_string<Args...> fmt, Args&&... args) {
    os << fmt::format(fmt, args...);
  }

  template <typename... Args>
  void print_line(const fmt::text_style& ts, const Args&... args) {
    os << fmt::format("{}", std::string(depth * 2, ' '));
    if (has_file_output) {
      os << fmt::format("{}", args...);
    } else {
      os << fmt::format(ts, "{}", args...);
    }
  }

  template <typename... Args>
  void print_line(fmt::format_string<Args...> fmt, Args&&... args) {
    os << fmt::format("{}", std::string(depth * 2, ' '));
    os << fmt::format(fmt, args...);
  }

  void write_metadata();
  void write_lnast();

  void write_n_ary(std::string_view op);
  void write_prim_type_int(char sign);

#define LNAST_NODE(NAME, VERBAL) void write_##NAME();
#include "lnast_nodes.def"
};
