//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <charconv>
#include <cstdint>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <stack>
#include <vector>

#include "ir_adapter.hpp"
#include "lnast.hpp"
#include "lnast_writer.hpp"
#include "upass_utils.hpp"

namespace upass {

struct Lnast_manager : public IR_adapter {
public:
  explicit Lnast_manager(const std::shared_ptr<Lnast>& ln) : lnast(ln), wr(std::cout, ln) {
    nid_stack   = {};
    current_nid = Lnast_nid::root();
  }
  virtual ~Lnast_manager() = default;
  Lnast_manager()          = delete;

  std::string_view kind() const override { return "lnast"; }
  std::size_t      node_count() const override {
    std::size_t count = 0;
    for (const auto& nid : lnast->depth_preorder(Lnast_nid::root())) {
      if (!nid.is_invalid()) {
        ++count;
      }
    }
    return count;
  }
  std::size_t const_count() const override {
    std::size_t count = 0;
    for (const auto& nid : lnast->depth_preorder(Lnast_nid::root())) {
      if (nid.is_invalid()) {
        continue;
      }
      if (lnast->get_type(nid).is_const()) {
        ++count;
      }
    }
    return count;
  }
  std::size_t arithmetic_count() const override {
    std::size_t count = 0;
    for (const auto& nid : lnast->depth_preorder(Lnast_nid::root())) {
      if (nid.is_invalid()) {
        continue;
      }
      switch (lnast->get_type(nid).get_raw_ntype()) {
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
  std::size_t fold_candidate_count() const override {
    // LNAST-side shared heuristic: treat arithmetic/comparison ops as fold candidates.
    return arithmetic_count();
  }
  std::vector<Node_id> list_nodes() const override {
    std::vector<Node_id> nodes;
    nodes.reserve(node_count());
    for (const auto& nid : lnast->depth_preorder(Lnast_nid::root())) {
      if (!nid.is_invalid()) {
        nodes.emplace_back(encode_nid(nid));
      }
    }
    return nodes;
  }
  std::string_view op_name(Node_id node) const override {
    const auto nid = decode_nid(node);
    if (nid.is_invalid()) {
      return "invalid";
    }
    switch (lnast->get_type(nid).get_raw_ntype()) {
      case Lnast_ntype::Lnast_ntype_const: return "const";
      case Lnast_ntype::Lnast_ntype_plus: return "sum";
      case Lnast_ntype::Lnast_ntype_minus: return "sub";
      case Lnast_ntype::Lnast_ntype_mult: return "mult";
      case Lnast_ntype::Lnast_ntype_div: return "div";
      case Lnast_ntype::Lnast_ntype_mod: return "mod";
      case Lnast_ntype::Lnast_ntype_shl: return "shl";
      case Lnast_ntype::Lnast_ntype_sra: return "sra";
      case Lnast_ntype::Lnast_ntype_bit_and: return "and";
      case Lnast_ntype::Lnast_ntype_bit_or: return "or";
      case Lnast_ntype::Lnast_ntype_bit_xor: return "xor";
      case Lnast_ntype::Lnast_ntype_assign: return "assign";
      default: return "other";
    }
  }
  std::vector<Node_id> inputs(Node_id node) const override {
    std::vector<Node_id> inps;
    const auto           nid = decode_nid(node);
    if (nid.is_invalid()) {
      return inps;
    }

    for (auto child = lnast->get_child(nid); !child.is_invalid(); child = lnast->get_sibling_next(child)) {
      inps.emplace_back(encode_nid(child));
    }

    return inps;
  }
  bool is_const(Node_id node) const override {
    const auto nid = decode_nid(node);
    if (nid.is_invalid()) {
      return false;
    }
    return lnast->get_type(nid).is_const();
  }
  std::optional<std::int64_t> const_value(Node_id node) const override {
    if (!is_const(node)) {
      return std::nullopt;
    }
    const auto text = lnast->get_data(decode_nid(node)).token.get_text();
    int64_t    value{0};
    auto*      begin = text.data();
    auto*      end   = text.data() + text.size();
    auto       res   = std::from_chars(begin, end, value);
    if (res.ec != std::errc() || res.ptr != end) {
      return std::nullopt;
    }
    return value;
  }
  Replace_effect estimate_replace_with_const(Node_id node) const override {
    if (decode_nid(node).is_invalid()) {
      return {};
    }
    // LNAST currently rewrites in place for shared const replacement.
    return {};
  }
  bool replace_with_const(Node_id node, std::int64_t value) override {
    const auto nid = decode_nid(node);
    if (nid.is_invalid()) {
      return false;
    }

    if (auto cur = const_value(node); cur && *cur == value) {
      return false;
    }

    // Keep subtree structure intact for now; shared transform passes will own any
    // operand cleanup when they start mutating LNAST through this API.
    lnast->set_data(nid, Lnast_node::create_const(value));
    return true;
  }

  auto get_top_module_name() { return lnast->get_top_module_name(); }

  void move_to_nid(const Lnast_nid& nid) { current_nid = nid; }

  auto current_text() const { return lnast->get_data(current_nid).token.get_text(); }

  // Returns a copy of the full Lnast_node at the read cursor. Used by the
  // runner to replicate the input node into the staging tree.
  Lnast_node current_node() const { return lnast->get_data(current_nid); }

  // Cursor-state snapshot used by the runner to recover from a pass that
  // mishandled its own move_to_* calls (e.g. an exception escaped before
  // move_to_parent). Not a replacement for correct balancing inside each
  // pass — it just keeps one buggy pass from corrupting the rest.
  struct Cursor_state {
    Lnast_nid             current;
    std::stack<Lnast_nid> stack;
  };
  Cursor_state save_cursor() const { return {current_nid, nid_stack}; }
  void         restore_cursor(const Cursor_state& s) {
    current_nid = s.current;
    nid_stack   = s.stack;
  }

  // True iff the read cursor has at least one child. Does not move the
  // cursor — safe to call before deciding whether to recurse.
  bool has_child() const { return !lnast->get_child(current_nid).is_invalid(); }

  virtual bool move_to_child() {
    nid_stack.push(current_nid);
    current_nid = lnast->get_child(current_nid);
    return !current_nid.is_invalid();
  }

  virtual bool move_to_sibling() {
    if (current_nid.is_invalid()) {
      return false;
    }
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
  static Node_id encode_nid(const Lnast_nid& nid) {
    const auto level = static_cast<std::uint32_t>(nid.level);
    const auto pos   = static_cast<std::uint32_t>(nid.pos);
    return (static_cast<Node_id>(level) << 32) | static_cast<Node_id>(pos);
  }

  static Lnast_nid decode_nid(Node_id nid) {
    const auto level = static_cast<std::int32_t>((nid >> 32) & 0xFFFF'FFFFULL);
    const auto pos   = static_cast<std::int32_t>(nid & 0xFFFF'FFFFULL);
    return {level, pos};
  }

  const std::shared_ptr<Lnast>& lnast;
  std::stack<Lnast_nid>         nid_stack;
  Lnast_nid                     current_nid;
  Lnast_writer                  wr;
};

}  // namespace upass
