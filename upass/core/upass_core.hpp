//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <string_view>
#include <vector>

#include "lconst.hpp"
#include "lnast.hpp"
#include "lnast_manager.hpp"
#include "lnast_ntype.hpp"
#include "lnast_writer.hpp"
#include "upass_utils.hpp"

namespace upass {

enum class Emit_kind { emit, drop_subtree };

struct Emit_decision {
  Emit_kind kind{Emit_kind::emit};

  static constexpr Emit_decision emit_node() { return Emit_decision{Emit_kind::emit}; }
  static constexpr Emit_decision drop() { return Emit_decision{Emit_kind::drop_subtree}; }
};

struct uPass {
public:
  uPass(std::shared_ptr<Lnast_manager>& _lm) : lm(_lm) {}
  virtual ~uPass() = default;

  void begin_iteration() { changed = false; }
  bool has_changed() const { return changed; }

  // Called by the runner on every drop-candidate op-node (assign, plus, eq, …)
  // after its per-node process_* has populated any ST state. Returning `drop`
  // tells the runner to omit the node (and its subtree) from the staging tree.
  // Default: always emit.
  virtual Emit_decision classify_statement() { return Emit_decision::emit_node(); }

  // Called by the runner for every `ref <name>` operand it is about to emit
  // (RHS of an op, condition of an `if`, cassert operand). If any pass
  // returns a concrete Lconst, the runner writes `const <value>` in place of
  // the ref. First non-nullopt wins.
  virtual std::optional<Lconst> fold_ref(std::string_view /*name*/) { return std::nullopt; }

#define PROCESS_NODE(NAME) \
  virtual void process_##NAME() {}

  // Assignment
  PROCESS_NODE(assign)
  PROCESS_NODE(delay_assign)

  // Bitwidth
  PROCESS_NODE(bit_and)
  PROCESS_NODE(bit_or)
  PROCESS_NODE(bit_not)
  PROCESS_NODE(bit_xor)

  // Bitwidth Insensitive Reduce
  PROCESS_NODE(red_or)
  PROCESS_NODE(red_and)
  PROCESS_NODE(red_xor)

  // Logical
  PROCESS_NODE(log_and)
  PROCESS_NODE(log_or)
  PROCESS_NODE(log_not)

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

  // Function Call
  PROCESS_NODE(func_call)
  PROCESS_NODE(func_def)

  // Tuple Operations
  PROCESS_NODE(tuple_get)
  PROCESS_NODE(tuple_set)
  PROCESS_NODE(tuple_add)
  PROCESS_NODE(tuple_concat)

  // Attributes
  PROCESS_NODE(attr_set)
  PROCESS_NODE(attr_get)

  // Control flow
  PROCESS_NODE(for)
  PROCESS_NODE(while)
  PROCESS_NODE(uif)
  PROCESS_NODE(range)
  PROCESS_NODE(cassert)

  // Types
  PROCESS_NODE(type_def)
  PROCESS_NODE(type_spec)

  // Structure
  PROCESS_NODE(top)
  PROCESS_NODE(stmts)
  PROCESS_NODE(if)

#undef PROCESS_NODE

protected:
  std::shared_ptr<Lnast_manager>& lm;
  bool                            changed{false};

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
  void mark_changed() { changed = true; }

  template <class... Lnast_ntype_int>
  bool is_type(Lnast_ntype_int... ty) const {
    auto n = get_raw_ntype();
    return (((n == ty) || ...) || false);
  }
};

struct uPass_node : public uPass {
public:
  using uPass::uPass;
};

struct uPass_struct : uPass {
public:
  using uPass::uPass;
};

template <class T>
struct uPass_wrapper {
public:
  static std::shared_ptr<uPass> get_upass(std::shared_ptr<Lnast_manager>& lm) { return std::make_unique<T>(lm); }
};

class uPass_plugin {
public:
  using Setup_fn = std::function<std::shared_ptr<uPass>(std::shared_ptr<Lnast_manager>&)>;
  struct Setup_entry {
    Setup_fn                 setup_fn;
    std::vector<std::string> depends_on;
  };
  using Map_setup = std::map<std::string, Setup_entry>;

protected:
  static inline Map_setup registry;

public:
  uPass_plugin(const std::string& name, Setup_fn setup_fn, std::vector<std::string> depends_on = {}) {
    if (registry.find(name) != registry.end()) {
      upass::error("uPass_plugin: {} is already registered\n", name);
      return;
    }
    registry[name] = Setup_entry{.setup_fn = std::move(setup_fn), .depends_on = std::move(depends_on)};
  }

  static const Map_setup& get_registry() { return registry; }
};

}  // namespace upass
