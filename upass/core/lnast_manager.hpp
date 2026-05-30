//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <charconv>
#include <cstdint>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <unordered_set>
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
  // copying. While an inline frame is active (push_source) the name of a
  // *ref* node is rewritten with the frame's per-call-site tag so callee
  // variables don't collide with caller (or other call-site) names — see
  // 1i in TODO_livehd.md. const names (literals / attr-keys) are never
  // rewritten. The rewritten string is interned so the view stays valid.
  std::string_view current_text() const {
    auto raw = lnast->get_name(current_nid);
    if (active_tag_.empty() || !Lnast_ntype::is_ref(lnast->get_type(current_nid))) {
      return raw;
    }
    // A tuple-literal field key (`(x = …)` lowers to assign(ref 'x', val) under
    // a tuple_add/concat/set) is a structural label, not a variable. Renaming
    // it (x → <tag>x) would change the bundle's field name and break `t.x`
    // lookups after inlining — leave it raw.
    if (is_tuple_field_key(current_nid)) {
      return raw;
    }
    // Tmps (`___N`) must stay `___<digits>` tmps — several ops (attr_get dst,
    // etc.) and DCE require it — so remap to a fresh globally-unique tmp
    // number rather than `<tag>___N`. User vars get the readable `<tag>name`.
    if (raw.size() >= 3 && raw[0] == '_' && raw[1] == '_' && raw[2] == '_') {
      return rename_tmp(raw);
    }
    return intern(make_inlined_name(active_tag_, raw));
  }
  auto current_type() const { return lnast->get_type(current_nid); }

  // Un-renamed name of the current node — for reading a func_call's callee
  // name (a function identifier, not a variable to rename) even inside an
  // active inline frame.
  std::string_view current_raw_text() const { return lnast->get_name(current_nid); }

  // Returns the Lnast_nid of the read cursor. Used by passes that need a
  // stable identity for the current node (e.g. for cross-pass cassert
  // dedup keyed by source location).
  Lnast_nid get_current_nid() const { return current_nid; }

  // Returns a detached copy of a ref/const leaf at the read cursor.
  // Structural nodes are represented by current_type().
  Lnast_node current_node() const {
    const auto type = lnast->get_type(current_nid);
    if (Lnast_ntype::is_ref(type)) {
      // current_text() applies the inline-frame rename for refs.
      return Lnast_node::create_ref(current_text());
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

  // ── Inline source frames (1i) ───────────────────────────────────────────
  // push_source swaps the active read tree to `callee` (a body that lives in
  // some other Lnast, e.g. a function_registry entry) with a fresh cursor at
  // its root, and activates a per-call-site rename `tag` + `salt`. The caller
  // tree/cursor/tag are saved and restored by pop_source. The callee body is
  // read in place — never copied (see 1i in TODO_livehd.md).
  void push_source(const std::shared_ptr<Lnast>& callee, std::string tag, uint32_t salt) {
    frames_.push_back(Source_frame{lnast, current_nid, std::move(nid_stack), std::move(active_tag_), active_salt_});
    lnast       = callee;
    nid_stack   = std::stack<Lnast_nid>{};
    current_nid = lnast->get_root();
    active_tag_  = std::move(tag);
    active_salt_ = salt;
  }
  void pop_source() {
    I(!frames_.empty());
    auto& f      = frames_.back();
    lnast        = std::move(f.tree);
    current_nid  = f.saved_cur;
    nid_stack    = std::move(f.saved_stack);
    active_tag_  = std::move(f.tag);
    active_salt_ = f.salt;
    frames_.pop_back();
  }
  bool        in_inline_frame() const { return !frames_.empty(); }
  std::size_t inline_depth() const { return frames_.size(); }
  std::string_view active_tag() const { return active_tag_; }

  // Scope identity for nid-keyed pass state (constprop block scopes). Plain
  // class_index collides across the caller/callee trees during a source
  // swap; mixing the frame salt into the high bits keeps frames distinct.
  // salt 0 (no active frame) reproduces the bare class_index.
  uint64_t current_scope_uid() const {
    const uint64_t ci = static_cast<uint64_t>(current_nid.get_class_index().value);
    return (static_cast<uint64_t>(active_salt_) << 40) ^ ci;
  }

  // Builds the inlined name for `raw` under `tag` (refs only). Uniform
  // `<tag><raw>` prefix — a plain [A-Za-z0-9_] identifier that lnastfmt
  // accepts (the `___N` tmp form requires a pure-digit suffix, so a
  // tmp-preserving scheme like `___<tag>N` would be rejected). Exposed
  // static so the runner can synthesize prologue/epilogue binding names
  // that match what current_text() emits for the body. Inlined names are
  // therefore non-tmps; DCE-of-inlined-temporaries is a 1i follow-up.
  static std::string make_inlined_name(std::string_view tag, std::string_view raw) {
    return std::string(tag) + std::string(raw);
  }

  // True iff the read cursor has at least one child. Does not move the
  // cursor — safe to call before deciding whether to recurse.
  bool has_child() const { return !lnast->get_child(current_nid).is_invalid(); }

  // Number of direct children of the cursor node. Does not move the cursor.
  // Used by the `store` dispatch arity branch (0 levels ⇒ 2 children ⇒ assign
  // path; ≥1 level ⇒ ≥3 children ⇒ tuple_set path).
  size_t current_num_children() const {
    size_t n = 0;
    for (auto c = lnast->get_child(current_nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      ++n;
    }
    return n;
  }

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
  // Owned (was a const ref) so an inline frame can swap the active read tree
  // to a callee body and restore the caller afterward (1i). Holding a
  // shared_ptr copy is one refcount bump at construction — negligible.
  std::shared_ptr<Lnast> lnast;
  std::stack<Lnast_nid>  nid_stack;
  Lnast_nid              current_nid;
  Lnast_writer           wr;

  // Saved caller state for one level of inline-source nesting.
  struct Source_frame {
    std::shared_ptr<Lnast> tree;
    Lnast_nid              saved_cur;
    std::stack<Lnast_nid>  saved_stack;
    std::string            tag;
    uint32_t               salt;
  };
  std::vector<Source_frame> frames_;
  std::string               active_tag_;
  uint32_t                  active_salt_{0};

  // Stable storage for renamed names so current_text()'s string_view stays
  // valid for the manager's lifetime. std::unordered_set is node-based, so
  // element addresses stay stable across rehash and the view never dangles.
  mutable std::unordered_set<std::string> intern_pool_;

  std::string_view intern(std::string&& s) const {
    auto it = intern_pool_.insert(std::move(s)).first;
    return std::string_view(*it);
  }

  // Per-(frame, original-tmp) → fresh `___<N>` mapping, consistent within a
  // frame so repeated reads of the same callee tmp resolve identically. The
  // base is high to avoid colliding with caller tmps (which are small).
  mutable std::unordered_map<std::string, std::string> tmp_remap_;
  mutable uint32_t                                      tmp_counter_{0};
  static constexpr uint32_t                            kInlineTmpBase = 900000;

  // True when `nid` is the key (child 0) of an `assign` that is a field entry
  // of a tuple_add / tuple_concat — i.e. the `x` in `(x = v)`.
  // Such refs are structural labels and must not be inline-renamed.
  bool is_tuple_field_key(const Lnast_nid& nid) const {
    auto parent = lnast->get_parent(nid);
    if (parent.is_invalid() || !Lnast_ntype::is_store(lnast->get_type(parent))) {
      return false;
    }
    if (lnast->get_first_child(parent) != nid) {
      return false;  // the value side, not the key
    }
    auto gp = lnast->get_parent(parent);
    if (gp.is_invalid()) {
      return false;
    }
    const auto gt = lnast->get_type(gp);
    return gt == Lnast_ntype::Lnast_ntype_tuple_add || gt == Lnast_ntype::Lnast_ntype_tuple_concat;
  }

  std::string_view rename_tmp(std::string_view raw) const {
    std::string key = std::to_string(active_salt_) + ":" + std::string(raw);
    auto        it  = tmp_remap_.find(key);
    if (it != tmp_remap_.end()) {
      return std::string_view(it->second);
    }
    std::string fresh = "___" + std::to_string(kInlineTmpBase + (++tmp_counter_));
    auto        ins   = tmp_remap_.emplace(std::move(key), std::move(fresh)).first;
    return std::string_view(ins->second);
  }
};

}  // namespace upass
