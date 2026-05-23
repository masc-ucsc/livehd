//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <charconv>
#include <cstdint>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <stack>
#include <utility>
#include <vector>

#include "const.hpp"
#include "ir_adapter.hpp"
#include "lnast.hpp"
#include "lnast_writer.hpp"
#include "upass_utils.hpp"

namespace upass {

struct Lnast_manager {
public:
  // Public node-handle type used by the templated shared passes. Lnast_nid
  // already aliases to `hhds::Tree::Node_class`, so the iterators return the
  // same handle the rest of LNAST works with — no encode/decode needed.
  using Node  = Lnast_nid;
  using Input = Lnast_nid;

  explicit Lnast_manager(const std::shared_ptr<Lnast>& ln) : lnast(ln), wr(std::cout, ln) {
    nid_stack   = {};
    current_nid = lnast->get_root();
  }
  virtual ~Lnast_manager() = default;
  Lnast_manager()          = delete;

  std::string_view kind() const { return "lnast"; }
  std::size_t      node_count() const {
    std::size_t count = 0;
    for (const auto& nid : lnast->depth_preorder(lnast->get_root())) {
      if (!nid.is_invalid()) {
        ++count;
      }
    }
    return count;
  }
  std::size_t const_count() const {
    std::size_t count = 0;
    for (const auto& nid : lnast->depth_preorder(lnast->get_root())) {
      if (nid.is_invalid()) {
        continue;
      }
      if (Lnast_ntype::is_const(lnast->get_type(nid))) {
        ++count;
      }
    }
    return count;
  }
  std::size_t arithmetic_count() const {
    std::size_t count = 0;
    for (const auto& nid : lnast->depth_preorder(lnast->get_root())) {
      if (nid.is_invalid()) {
        continue;
      }
      switch (lnast->get_type(nid)) {
        case Lnast_ntype::Lnast_ntype_plus:
        case Lnast_ntype::Lnast_ntype_minus:
        case Lnast_ntype::Lnast_ntype_mult:
        case Lnast_ntype::Lnast_ntype_div:
        case Lnast_ntype::Lnast_ntype_mod:
        case Lnast_ntype::Lnast_ntype_shl:
        case Lnast_ntype::Lnast_ntype_sra:
        case Lnast_ntype::Lnast_ntype_bit_and:
        case Lnast_ntype::Lnast_ntype_bit_or:
        case Lnast_ntype::Lnast_ntype_bit_xor:
        case Lnast_ntype::Lnast_ntype_log_and:
        case Lnast_ntype::Lnast_ntype_log_or:
        case Lnast_ntype::Lnast_ntype_eq:
        case Lnast_ntype::Lnast_ntype_ne:
        case Lnast_ntype::Lnast_ntype_lt:
        case Lnast_ntype::Lnast_ntype_le:
        case Lnast_ntype::Lnast_ntype_gt:
        case Lnast_ntype::Lnast_ntype_ge: ++count; break;
        default: break;
      }
    }
    return count;
  }
  std::size_t fold_candidate_count() const {
    // LNAST-side shared heuristic: treat arithmetic/comparison ops as fold candidates.
    return arithmetic_count();
  }

  // Streams every node through `fn` using `lnast->depth_preorder()` directly —
  // the callback receives the same `Lnast_nid` the LNAST API works with.
  template <typename Fn>
  void for_each_node(Fn&& fn) const {
    for (const auto& nid : lnast->depth_preorder(lnast->get_root())) {
      if (!nid.is_invalid()) {
        std::forward<Fn>(fn)(nid);
      }
    }
  }

  template <typename Fn>
  void for_each_input(const Node& node, Fn&& fn) const {
    if (node.is_invalid()) {
      return;
    }
    for (auto child = lnast->get_child(node); !child.is_invalid(); child = lnast->get_sibling_next(child)) {
      std::forward<Fn>(fn)(child);
    }
  }

  bool is_sum_op(const Node& node) const { return !node.is_invalid() && Lnast_ntype::is_plus(lnast->get_type(node)); }
  bool is_const(const Input& node) const { return !node.is_invalid() && Lnast_ntype::is_const(lnast->get_type(node)); }

  std::optional<Const> const_value(const Input& node) const {
    if (!is_const(node)) {
      return std::nullopt;
    }
    const auto text = lnast->get_name(node);
    int64_t    value{0};
    auto*      begin = text.data();
    auto*      end   = text.data() + text.size();
    auto       res   = std::from_chars(begin, end, value);
    if (res.ec != std::errc() || res.ptr != end) {
      return std::nullopt;
    }
    return *Dlop::create_integer(value);
  }

  Replace_effect estimate_replace_with_const(const Node& node) const {
    (void)node;
    // LNAST rewrites in place — no edge rewiring, no nodes torn down.
    return {};
  }

  bool replace_with_const(const Node& node, const Const& value) {
    if (node.is_invalid()) {
      return false;
    }
    if (auto cur = const_value(node); cur && cur->same_repr(value)) {
      return false;
    }
    // Keep subtree structure intact for now; shared transform passes will own any
    // operand cleanup when they start mutating LNAST through this API.
    lnast->set_data(node, Lnast_node::create_const(value.to_i()));
    return true;
  }

  auto                          get_top_module_name() { return lnast->get_top_module_name(); }
  const std::shared_ptr<Lnast>& get_lnast() const { return lnast; }

  void move_to_nid(const Lnast_nid& nid) { current_nid = nid; }

  // Returns a string_view backed by the persistent attribute store, so it
  // stays valid until the LNAST mutates that node's text. Cheaper than
  auto current_text() const { return lnast->get_name(current_nid); }
  auto current_type() const { return lnast->get_type(current_nid); }

  // Returns the Lnast_nid of the read cursor. Used by passes that need a
  // stable identity for the current node (e.g. for cross-pass cassert
  // dedup keyed by source location).
  Lnast_nid get_current_nid() const { return current_nid; }

  // Returns a detached copy of a ref/const leaf at the read cursor.
  // Structural nodes are represented by current_type().
  Lnast_node current_node() const {
    const auto type = lnast->get_type(current_nid);
    if (Lnast_ntype::is_ref(type)) {
      return Lnast_node::create_ref(lnast->get_name(current_nid));
    }
    if (Lnast_ntype::is_const(type)) {
      return Lnast_node::create_const(lnast->get_name(current_nid));
    }
    I(Lnast_ntype::is_invalid(type));
    return Lnast_node::create_invalid();
  }

  // Cursor-state snapshot used by the runner to recover from a pass that
  // mishandled its own move_to_* calls (e.g. an exception escaped before
  // move_to_parent). Not a replacement for correct balancing inside each
  // pass — it just keeps one buggy pass from corrupting the rest.
  //
  // We only snapshot `current_nid` and the stack depth: in the balanced
  // (common) case both are unchanged across the pass, and on imbalance the
  // pass can only have *pushed* further than where it started (move_to_parent
  // asserts on underflow). Restore pops back to the saved depth, which is the
  // cheap path — no deque copy per dispatch.
  struct Cursor_state {
    Lnast_nid   current;
    std::size_t depth;
  };
  Cursor_state save_cursor() const { return {current_nid, nid_stack.size()}; }
  void         restore_cursor(const Cursor_state& s) {
    current_nid = s.current;
    while (nid_stack.size() > s.depth) {
      nid_stack.pop();
    }
  }

  // True iff the read cursor has at least one child. Does not move the
  // cursor — safe to call before deciding whether to recurse.
  bool has_child() const { return !lnast->get_child(current_nid).is_invalid(); }

  // Cursor moves are non-virtual: there are no subclasses of Lnast_manager
  // in-tree, and these are on every per-op pass's hot loop (≈10M+ calls per
  // pass.upass run on xx.prp). Devirtualizing lets the compiler inline the
  // single-pointer body the rest of the codebase actually uses.
  bool move_to_child() {
    nid_stack.push(current_nid);
    current_nid = lnast->get_child(current_nid);
    return !current_nid.is_invalid();
  }

  bool move_to_sibling() {
    if (current_nid.is_invalid()) {
      return false;
    }
    current_nid = lnast->get_sibling_next(current_nid);
    return !current_nid.is_invalid();
  }

  void move_to_parent() {
    I(nid_stack.size() >= 1);
    current_nid = nid_stack.top();
    nid_stack.pop();
  }

  auto get_ntype() const { return lnast->get_type(current_nid); }

  auto get_raw_ntype() const { return lnast->get_type(current_nid); }

  bool is_invalid() const { return current_nid.is_invalid(); }

  bool is_last_child() const {
    if (current_nid.is_invalid()) {
      return false;
    }
    return lnast->is_last_child(current_nid);
  }

  void write_node() {
#ifndef NDEBUG
    wr.write_nid(current_nid);
    std::cout << "\n";
#endif
  }

protected:
  const std::shared_ptr<Lnast>& lnast;
  std::stack<Lnast_nid>         nid_stack;
  Lnast_nid                     current_nid;
  Lnast_writer                  wr;
};

}  // namespace upass
