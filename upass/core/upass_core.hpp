//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <stack>
#include <vector>
#include "lnast.hpp"
#include "upass_utils.hpp"
#include "lnast_writer.hpp"

namespace upass {

struct uPass {
public:
  uPass(std::shared_ptr<Lnast_manager>& _lm) : lm(_lm) {}

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

  // Structure
  PROCESS_NODE(top)
  PROCESS_NODE(stmts)

#undef PROCESS_NODE

protected:
  std::shared_ptr<Lnast_manager>& lm;

  void move_to_nid(const Lnast_nid& nid) { lm->move_to_nid(nid); }
  auto current_text() const { return lm->current_text(); }
  auto move_to_child() { return lm->move_to_child(); }
  auto move_to_sibling() { return lm->move_to_sibling(); }
  void move_to_parent() { lm->move_to_parent(); }
  auto get_ntype() const { return lm->get_ntype(); }
  auto get_raw_ntype() const { return lm->get_raw_ntype(); }
  bool is_invalid() const { return lm->is_invalid(); }
  bool is_last_child() const { return lm->is_last_child(); }
  void write_node() { lm->write_node(); }

};

struct uPass_node : public uPass {
public:
  using uPass::uPass;
};

struct uPass_struct : uPass {
public:
  using uPass::uPass;
};

template<class T>
struct uPass_wrapper {
public:
  static std::shared_ptr<uPass> get_upass(std::shared_ptr<Lnast_manager>& lm) {
    return std::make_unique<T>(lm);
  }
};

class uPass_plugin {
public:
  using Setup_fn  = std::function<std::shared_ptr<uPass>(std::shared_ptr<Lnast_manager>&)>;
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
