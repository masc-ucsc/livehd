//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <stack>
#include <vector>
#include "lnast.hpp"
#include "upass_utils.hpp"

namespace upass {

struct Lnast_traversal {
public:
  explicit Lnast_traversal(const std::shared_ptr<Lnast>& ln) : lnast(ln) {
    nid_stack = {};
    current_nid = Lnast_nid::root();
  }
  Lnast_traversal() = default;

  void move_to_nid(const Lnast_nid& nid) {
    current_nid = nid;
  }

protected:
  const std::shared_ptr<Lnast>& lnast;
  std::stack<Lnast_nid> nid_stack;
  Lnast_nid current_nid;

  auto current_text() const {
    return lnast->get_data(current_nid).token.get_text();
  }

  virtual bool move_to_child() {
    nid_stack.push(current_nid);
    current_nid = lnast->get_child(current_nid);
    return !current_nid.is_invalid();
  }

  virtual bool move_to_sibling() {
    if (current_nid.is_invalid()) return false;
    current_nid = lnast->get_sibling_next(current_nid);
    return !current_nid.is_invalid();
  }

  virtual void move_to_parent() {
    I(nid_stack.size() >= 1);
    current_nid = nid_stack.top();
    nid_stack.pop();
  }
  
  auto get_ntype() const {
    return lnast->get_type(current_nid);
  }

  auto get_raw_ntype() const {
    return lnast->get_type(current_nid).get_raw_ntype();
  }

  bool is_invalid() const {
    return current_nid.is_invalid();
  }

  bool is_last_child() const {
    if (current_nid.is_invalid()) return false;
    return lnast->is_last_child(current_nid);
  }

};

struct uPass : public Lnast_traversal {
public:
  using Lnast_traversal::Lnast_traversal;

#define PROCESS_NODE(NAME) \
  virtual void process_##NAME() {}

  // Assignment
  PROCESS_NODE(assign)

  // Bitwidth
  PROCESS_NODE(bit_and)
  PROCESS_NODE(bit_or)
  PROCESS_NODE(bit_not)
  PROCESS_NODE(bit_xor)
  
  // Bitwidth Insensitive Reduce
  PROCESS_NODE(reduce_or)
  
  // Logical
  PROCESS_NODE(logical_and)
  PROCESS_NODE(logical_or)
  PROCESS_NODE(logical_not)
  
  // Arithmetic
  PROCESS_NODE(plus)
  PROCESS_NODE(minus)
  PROCESS_NODE(mult)
  PROCESS_NODE(div)
  PROCESS_NODE(mod)
  
  // Shift
  PROCESS_NODE(shl)
  PROCESS_NODE(sra)
  
  // Bit Manipulation
  PROCESS_NODE(sext)
  PROCESS_NODE(set_mask)
  PROCESS_NODE(get_mask)
  PROCESS_NODE(mask_and)
  PROCESS_NODE(mask_popcount)
  PROCESS_NODE(mask_xor)
  
  // Comparison
  PROCESS_NODE(ne)
  PROCESS_NODE(eq)
  PROCESS_NODE(lt)
  PROCESS_NODE(le)
  PROCESS_NODE(gt)
  PROCESS_NODE(ge)

#undef PROCESS_NODE

 };

template<class T>
struct uPass_wrapper {
public:
  static std::shared_ptr<uPass> get_upass(const std::shared_ptr<Lnast>& ln) {
    return std::make_unique<T>(ln);
  }
};

class uPass_plugin {
public:
  using Setup_fn  = std::function<std::shared_ptr<upass::uPass>(const std::shared_ptr<Lnast>&)>;
  using Map_setup = std::map<std::string, Setup_fn>;

protected:
  static inline Map_setup registry;

public:
  uPass_plugin(const std::string &name, Setup_fn setup_fn) {
    if (registry.find(name) != registry.end()) {
      upass::error("uPass_plugin: {} is already registered", name);
      return;
    }
    registry[name] = setup_fn;
  }

  static const Map_setup &get_registry() { return registry; }
};
}
