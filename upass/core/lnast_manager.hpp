//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <stack>
#include <vector>

#include "lnast.hpp"
#include "lnast_writer.hpp"
#include "upass_utils.hpp"

namespace upass {

struct Lnast_manager {
public:
  explicit Lnast_manager(const std::shared_ptr<Lnast>& ln) : lnast(ln), wr(std::cout, ln) {
    nid_stack   = {};
    current_nid = Lnast_nid::root();
  }
  Lnast_manager() = delete;

  auto get_top_module_name() { return lnast->get_top_module_name(); }

  void move_to_nid(const Lnast_nid& nid) { current_nid = nid; }

  auto current_text() const { return lnast->get_data(current_nid).token.get_text(); }

  virtual bool move_to_child() {
    nid_stack.push(current_nid);
    current_nid = lnast->get_child(current_nid);
    return !current_nid.is_invalid();
  }

  virtual bool move_to_sibling() {
    if (current_nid.is_invalid())
      return false;
    current_nid = lnast->get_sibling_next(current_nid);
    return !current_nid.is_invalid();
  }

  virtual void move_to_parent() {
    I(nid_stack.size() >= 1);
    current_nid = nid_stack.top();
    nid_stack.pop();
  }

  auto get_ntype() const { return lnast->get_type(current_nid); }

  auto get_raw_ntype() const { return lnast->get_type(current_nid).get_raw_ntype(); }

  bool is_invalid() const { return current_nid.is_invalid(); }

  bool is_last_child() const {
    if (current_nid.is_invalid())
      return false;
    return lnast->is_last_child(current_nid);
  }

  void write_node() {
#ifndef NDEBUG
    wr.write_nid(current_nid);
    fmt::print("\n");
#endif
  }

protected:
  const std::shared_ptr<Lnast>& lnast;
  std::stack<Lnast_nid>         nid_stack;
  Lnast_nid                     current_nid;
  Lnast_writer                  wr;
};

}  // namespace upass
