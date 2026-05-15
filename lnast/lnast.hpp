//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <format>
#include <iostream>
#include <memory>
#include <print>
#include <stack>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
#include "hhds/attrs/name.hpp"
#include "hhds/tree.hpp"
#include "lnast_attrs.hpp"
#include "lnast_ntype.hpp"

// Local replacements for the legacy `(level, pos)` accessors. `level_of`
// walks the parent chain, so it's diagnostic-only — algorithmic users
// (pass/locator) carry depth along their pre-order walk instead.
inline int32_t level_of(const hhds::Tree::Node_class& nid) {
  int32_t d = 0;
  auto    p = nid.parent();
  while (p.is_valid()) {
    ++d;
    p = p.parent();
  }
  return d;
}
inline int64_t pos_of(const hhds::Tree::Node_class& nid) { return nid.get_class_index().value; }

using Lnast_nid = hhds::Tree::Node_class;

// Detached leaf payload. Structural nodes are just Lnast_ntype values and
// should be inserted with Lnast::add_child(parent, type). A detached
// Lnast_node is only needed when code must carry a ref/const result before it
// knows where that leaf will be inserted.
class Lnast_node {
public:
  enum class Kind : uint8_t { invalid, ref, constant };

  Lnast_node() = default;

  static Lnast_node create_invalid() { return Lnast_node(); }
  static Lnast_node create_ref(std::string_view name) { return Lnast_node(Kind::ref, std::string{name}); }
  static Lnast_node create_const(std::string_view value) { return Lnast_node(Kind::constant, std::string{value}); }
  static Lnast_node create_const(int64_t v) { return Lnast_node(Kind::constant, std::to_string(v)); }

  bool is_invalid() const { return kind == Kind::invalid; }
  bool is_ref() const { return kind == Kind::ref; }
  bool is_const() const { return kind == Kind::constant; }

  Lnast_ntype::Lnast_ntype_int get_type() const {
    switch (kind) {
      case Kind::invalid: return Lnast_ntype::create_invalid();
      case Kind::ref: return Lnast_ntype::create_ref();
      case Kind::constant: return Lnast_ntype::create_const();
    }
    return Lnast_ntype::create_invalid();
  }

  std::string_view get_name() const { return text; }
  void             dump() const;

private:
  Lnast_node(Kind k, std::string value) : kind(k), text(std::move(value)) {}

  Kind        kind{Kind::invalid};
  std::string text;
};

// ── Bitwidth metadata side-channel (populated by upass/bitwidth) ─────────────
// A lightweight POD mirror of Lnast_range stored on the Lnast after the
// bitwidth pass runs.  lnast_range.hpp is intentionally NOT included here to
// avoid a dependency between lnast/ and upass/bitwidth/.
struct BitwidthEntry {
  int64_t min{0};
  int64_t max{0};
  bool    neg_inf{true};  // min = −∞ (unbounded below)
  bool    pos_inf{true};  // max = +∞ (unbounded above)

  bool is_unbounded() const noexcept { return neg_inf || pos_inf; }
  bool is_constant()  const noexcept { return !neg_inf && !pos_inf && min == max; }
};

struct Lnast_bitwidth_meta {
  std::unordered_map<std::string, BitwidthEntry> ranges;
  bool empty() const noexcept { return ranges.empty(); }
};

// ── I/O metadata side-channel (populated by upass/ssa when ssa:1 is set) ────
// Separate from hhds::TreeIO (which is tree-replacement plumbing).
struct Lnast_io_entry {
  std::string name;          // field name, no $ / % prefix
  int32_t     bits     = 0;  // 0 = unknown / infer from context
  bool        is_signed = true;
  bool        is_ref    = false;  // input declared with `ref` → write-back on inline
};
struct Lnast_tree_io {
  std::vector<Lnast_io_entry> inputs;
  std::vector<Lnast_io_entry> outputs;
  bool empty() const noexcept { return inputs.empty() && outputs.empty(); }
};

class Lnast {
private:
  // Forest must outlive the tree — HHDS Tree::forest_ptr is a raw pointer
  // and TreeIO::forest_owner_ is weak, so dropping our shared_ptr would
  // leave forest_ptr dangling.
  std::shared_ptr<hhds::Forest> forest_;
  std::shared_ptr<hhds::TreeIO> treeio_;
  std::shared_ptr<hhds::Tree>   tree_;
  std::string                   top_module_name;
  std::string                   source_filename;
  Lnast_nid                     undefined_var_nid;
  // I/O metadata populated by the SSA upass (ssa:1).  Empty unless the SSA
  // pass has run on this LNAST.
  Lnast_tree_io                 io_meta_;
  // Bitwidth metadata populated by uPass_bitwidth::end_run().  Empty unless
  // the bitwidth pass has run on this LNAST.
  Lnast_bitwidth_meta           bw_meta_;

public:
  static constexpr char version[] = "0.1.0";

  explicit Lnast() : Lnast("noname", "") {}
  explicit Lnast(std::string_view _module_name) : Lnast(_module_name, "") {}
  Lnast(std::string_view _module_name, std::string_view _file_name);
  // Wrap an already-built unattached tree (no Forest, no TreeIO). Used by
  // the upass runner to wrap a `Forest::create_tree_temp` body so the
  // existing add_child / set_data API drives staging emission. The wrapper
  // cannot be replaced (no TreeIO); replace_body() asserts on this Lnast.
  Lnast(std::shared_ptr<hhds::Tree> body, std::string_view _module_name);
  ~Lnast();

  // ── tree access ─────────────────────────────────────────────────────────
  hhds::Tree&                          tree() noexcept { return *tree_; }
  const hhds::Tree&                    tree() const noexcept { return *tree_; }
  std::shared_ptr<hhds::Tree>          tree_ptr() const noexcept { return tree_; }
  const std::shared_ptr<hhds::Forest>& forest() const noexcept { return forest_; }
  const std::shared_ptr<hhds::TreeIO>& treeio() const noexcept { return treeio_; }

  // Atomically swap the tree body backing this Lnast. `new_body` must be an
  // unattached tree (e.g. from `forest()->create_tree_temp(...)` or
  // `tree_ptr()->clone()`). Slot identity (Tid + name) is preserved; any
  // other holder of this Lnast picks up the new body on next access.
  // Asserts when called on a Lnast constructed without a TreeIO.
  void replace_body(std::shared_ptr<hhds::Tree> new_body);

  Lnast_nid get_root() const { return tree_->get_root_node(); }

  // ── navigation forwarders (operate on Node_class internally) ────────────
  bool      is_root(const Lnast_nid& nid) const { return nid == get_root(); }
  bool      is_leaf(const Lnast_nid& nid) const { return nid.is_leaf(); }
  bool      is_first_child(const Lnast_nid& nid) const { return nid.is_first_child(); }
  bool      is_last_child(const Lnast_nid& nid) const { return nid.is_last_child(); }
  Lnast_nid get_parent(const Lnast_nid& nid) const { return nid.parent(); }
  Lnast_nid get_first_child(const Lnast_nid& nid) const { return nid.first_child(); }
  Lnast_nid get_last_child(const Lnast_nid& nid) const { return nid.last_child(); }
  Lnast_nid get_sibling_next(const Lnast_nid& nid) const { return nid.next_sibling(); }
  Lnast_nid get_sibling_prev(const Lnast_nid& nid) const { return nid.prev_sibling(); }
  Lnast_nid get_child(const Lnast_nid& nid) const { return nid.first_child(); }

  // True iff the node has exactly one child.
  bool has_single_child(const Lnast_nid& nid) const {
    auto fc = nid.first_child();
    return fc.is_valid() && fc.is_last_child();
  }

  // ── iteration ───────────────────────────────────────────────────────────
  // children(parent): visit each direct child of parent.
  auto children(const Lnast_nid& parent) const { return tree_->sibling_order(parent.first_child()); }
  // depth_preorder(start): walk subtree in pre-order. Yields Node_class.
  auto depth_preorder(const Lnast_nid& start) const { return tree_->pre_order(start); }
  auto depth_preorder() const { return tree_->pre_order(); }
  // depth_postorder uses HHDS's non-const post_order range; callers needing
  // post-order traversal should reach into the Node_class API directly.

  // ── mutation ────────────────────────────────────────────────────────────
  // Structural nodes are inserted with an explicit type. Detached
  // Lnast_node values are only for ref/const/invalid leaves.
  Lnast_nid set_root(Lnast_ntype::Lnast_ntype_int type);
  Lnast_nid set_root(const Lnast_node& n);
  Lnast_nid add_child(const Lnast_nid& parent, Lnast_ntype::Lnast_ntype_int type);
  Lnast_nid add_child(const Lnast_nid& parent, const Lnast_node& n);
  Lnast_nid append_sibling(const Lnast_nid& sibling, Lnast_ntype::Lnast_ntype_int type);
  Lnast_nid append_sibling(const Lnast_nid& sibling, const Lnast_node& n);

  // ── payload accessors ───────────────────────────────────────────────────
  Lnast_ntype::Lnast_ntype_int get_type(const Lnast_nid& nid) const;
  void                         set_type(const Lnast_nid& nid, Lnast_ntype::Lnast_ntype_int t);
  std::string_view             get_name(const Lnast_nid& nid) const;
  std::string_view             get_vname(const Lnast_nid& nid) const { return get_name(nid); }
  void                         set_name(const Lnast_nid& nid, std::string_view name);

  // LoC payload — read/write the per-node source-position attributes
  // independently of the name/type bundle. Most callers don't need this;
  // diagnostic prints and the parser do.
  struct Loc {
    uint64_t pos1{0};
    uint64_t pos2{0};
    uint32_t line{0};
    uint8_t  tok{0};
  };
  Loc              get_loc(const Lnast_nid& nid) const;
  void             set_loc(const Lnast_nid& nid, const Loc& loc);
  std::string_view get_fname(const Lnast_nid& nid) const;
  void             set_fname(const Lnast_nid& nid, std::string_view fname);

  // set_data: write-side helpers used by add_child / set_root /
  // append_sibling. On the read side, callers go through get_type/get_name.
  void set_data(const Lnast_nid& nid, Lnast_ntype::Lnast_ntype_int type);
  void set_data(const Lnast_nid& nid, const Lnast_node& n);

  // ── module / source metadata ────────────────────────────────────────────
  std::string_view get_top_module_name() const { return top_module_name; }
  std::string_view get_source() const { return source_filename; }
  void             set_top_module_name(std::string_view name) { top_module_name = name; }

  // ── I/O metadata side-channel (set by the SSA upass when ssa:1) ─────────
  const Lnast_tree_io& io_meta() const noexcept { return io_meta_; }
  Lnast_tree_io&       io_meta() noexcept { return io_meta_; }

  // ── Bitwidth metadata side-channel (set by uPass_bitwidth::end_run) ──────
  const Lnast_bitwidth_meta& bw_meta() const noexcept { return bw_meta_; }
  Lnast_bitwidth_meta&       bw_meta() noexcept { return bw_meta_; }

  // ── name predicates (work off the textual name only) ────────────────────
  static bool is_register(std::string_view name) { return !name.empty() && name.front() == '#'; }
  static bool is_output(std::string_view name) { return !name.empty() && name.front() == '%'; }
  static bool is_input(std::string_view name) { return !name.empty() && name.front() == '$'; }
  static bool is_tmp(std::string_view name) { return name.size() >= 3 && name.substr(0, 3) == "___"; }

  // ── print / dump / read ────────────────────────────────────────────────
  // print: pretty box-drawing tree for humans (hhds Tree::print).
  //   Not round-trippable. node_text shows "type: name".
  // dump: structured text via hhds Tree::write_dump. Round-trips through
  //   Lnast::read. node_text holds just the variable name; type comes from
  //   the type column. Per-node loc/fname ride as @(loc=..,fname=..).
  // read: parse a `dump` file back into an Lnast.
  void print(std::ostream& os, const Lnast_nid& root_nid) const;
  void print(std::ostream& os) const { print(os, get_root()); }
  void print() const { print(std::cout, get_root()); }

  void dump(std::ostream& os) const;
  void dump(const std::string& filename) const;
  void dump() const { dump(std::cout); }

  // read: parse a single tree from a dump. read_all: parse every tree in a
  // multi-tree dump (lnast.dump concatenates trees one after the other).
  static std::shared_ptr<Lnast>              read(std::istream& is);
  static std::shared_ptr<Lnast>              read(const std::string& filename);
  static std::vector<std::shared_ptr<Lnast>> read_all(std::istream& is);
  static std::vector<std::shared_ptr<Lnast>> read_all(const std::string& filename);

  template <typename... Args>
  static void info(std::format_string<Args...> format, Args&&... args) {
    auto txt = std::format(format, std::forward<Args>(args)...);
    std::print("info:{}\n", txt);
  }
  static void info(std::string_view txt) { std::print("info:{}\n", txt); }

  class error : public std::runtime_error {
  public:
    template <typename... Args>
    error(std::format_string<Args...> format, Args&&... args)
        : std::runtime_error(std::format(format, std::forward<Args>(args)...)) {
      std::print("error:lnast {}\n", what());
      throw std::runtime_error(std::string(what()));
    }
    error(std::string_view txt) : std::runtime_error(std::string(txt)) {
      std::print("error:lnast {}\n", what());
      throw std::runtime_error(std::string(what()));
    }
  };
};
