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
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
#include "diag.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/attrs/srcid.hpp"
#include "hhds/source_locator.hpp"
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
      case Kind::invalid : return Lnast_ntype::create_invalid();
      case Kind::ref     : return Lnast_ntype::create_ref();
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
  bool    unbounded{true};  // no derivable [min,max] (Goal 1n N3: no +inf/-inf)

  bool is_unbounded() const noexcept { return unbounded; }
  bool is_constant() const noexcept { return !unbounded && min == max; }
};

struct Lnast_bitwidth_meta {
  std::unordered_map<std::string, BitwidthEntry> ranges;
  bool                                           empty() const noexcept { return ranges.empty(); }
};

// ── I/O metadata side-channel (populated by upass/ssa when ssa:1 is set) ────
// Separate from hhds::TreeIO (which is tree-replacement plumbing).
// Scalar type kind of an I/O leaf, used for call-site argument disambiguation
// (06-functions.md §"Argument naming"): a positional actual may bind to a
// parameter when their kinds make the mapping unique. `none` = untyped (the
// param must then be named unless it is the only argument).
enum class Io_kind : uint8_t { none, integer, boolean, string };

struct Lnast_io_entry {
  std::string name      = {};          // field name, no $ / % prefix
  int32_t     bits      = 0;           // 0 = unknown / infer from context
  bool        is_signed = true;
  bool        is_ref    = false;       // input declared with `ref` → write-back on inline
  // Task 1p — input declared with `...` (var-args, `comb foo(...rest)`). The
  // marker rides the io store's default-value slot as `const "..."` (mirroring
  // the `ref` sentinel) and is harvested here by the SSA upass. A var-arg
  // param gathers every actual not consumed by a fixed leading param into one
  // synthesized tuple at the call site (the comb inliner) and flags the lambda
  // as a not-fully-typed template (func_extract).
  bool        is_varargs = false;
  Io_kind     kind      = Io_kind::none;  // scalar kind from the param's prim_type
  // Task 1q — pipe stages annotation (outputs of a `pipe` func_def only).
  // From the trailing `stages(min,max)` io node: min 0 = absent (comb/mod),
  // max 0 with min>0 = unconstrained (bare `pipe`). The LN pipe upass keys
  // its output-flop insertion off these; the declared range rides verbatim
  // (LG pass1 narrows by sigma later, never here).
  int32_t     stages_min = 0;
  int32_t     stages_max = 0;
  // Task 1k — declared NAMED type of the param (`self:t1` → "t1"); empty when
  // untyped or annotated with a primitive type. The inliner's typed-self
  // `does`-check keys off inputs[0].type_name.
  std::string type_name = {};
};
struct Lnast_tree_io {
  std::vector<Lnast_io_entry> inputs;
  std::vector<Lnast_io_entry> outputs;
  bool                        empty() const noexcept { return inputs.empty() && outputs.empty(); }
};

// Task 1m — one `pub` export of a file unit (the LiveHD docs).
// kind: "value" (comptime/const declaration) | "comb" | "mod" | "pipe" |
// "fluid" (exported definition; its tree url is `<unit>.<name>`).
// srcid ([[1f]]): the pub declaration's SourceId in the unit's locator —
// the kernel-synthesized `<unit>.__pub` wrapper anchors its nodes here.
// 0 when unknown (e.g. a manifest-restored pub list).
struct Lnast_pub_entry {
  std::string    name;
  std::string    kind;
  hhds::SourceId srcid = 0;
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
  Lnast_nid                     undefined_var_nid;
  // Task 1r — durable lambda kind ("comb" / "pipe" / "mod" / ...; empty =
  // file-level / unknown). Stamped by func_extract when a lambda is
  // extracted into its own tree. Consumers gate on THIS, never on
  // stages_min>0 — a mod output legitimately stamps stages(0,0) or
  // stages(nil,nil) and a mod tree must not be mistaken for a pipe.
  std::string                   lambda_kind_;
  // Task 1p — durable "deferred template" flag. A not-fully-typed lambda — an
  // untyped non-`self` input, a `...args` var-arg param, or an unbound generic
  // `<T>` — is kept as LNAST but emits NO LGraph at definition time; the
  // concrete form is produced per call site (`comb` inlines, `pipe`/`mod`/
  // `fluid` specialize into a Sub). Stamped by func_extract when the extracted
  // signature is not fully typed; cleared on a specialized clone. tolg + the
  // no-LGraph gate read it. In-memory only (sibling to lambda_kind_).
  bool                          template_ = false;
  // Task 1p — generic type parameters (`<T, U>`) recorded by func_extract from
  // the func_def generics child (a seam: the per-`T` body substitution lands
  // in a follow-up goal; this only preserves the names so a template carrying
  // generics is detected and its mangling reserved).
  std::vector<std::string>      generics_;
  // Task 1m — the file's `pub` export list, recorded by prp2lnast on the
  // file-level (top) Lnast. kind is "value" for `pub const`/`pub comptime`
  // declarations or the lambda kind ("comb"/"mod"/"pipe"/"fluid") for
  // exported definitions. In-memory only (like lambda_kind_): the durable
  // forms are the `<unit>.__pub` wrapper tree and the manifest pub index.
  std::vector<Lnast_pub_entry>  pub_list_;
  // Task 1m — folded comptime leaves of the pub VALUE exports, stamped by
  // uPass_constprop when the file-scope walk completes: (flat dotted path,
  // pyrope const text) pairs — a scalar contributes ("name", "12"), a bundle
  // one pair per data leaf ("cfg.gain", "2"). The kernel synthesizes the
  // `<unit>.__pub` wrapper from this (the materialized tree can't be used:
  // a fully-folded scalar const store is dropped from it).
  std::vector<std::pair<std::string, std::string>> pub_values_;
  // I/O metadata populated by the SSA upass (ssa:1).  Empty unless the SSA
  // pass has run on this LNAST.
  Lnast_tree_io                 io_meta_;
  // Bitwidth metadata populated by uPass_bitwidth::end_run().  Empty unless
  // the bitwidth pass has run on this LNAST.
  Lnast_bitwidth_meta           bw_meta_;
  // Source-provenance table ([[1f]]): nodes carry one uint64 SourceId
  // (hhds::attrs::srcid) resolved through this locator. One locator per Lnast
  // (the single-writer unit — no locks); it survives replace_body (the locator
  // belongs to the Lnast, not the tree body). adopt() chains it to the loaded
  // Forest's source_map as a read-only base; export_into unions it back.
  hhds::Source_locator          srcloc_;
  // While nonzero, set_data stamps every def-bearing node with this id (the
  // enclosing statement's span). Only producers (prp2lnast) drive it; copies
  // and staging rebuilds leave it 0 and carry srcid explicitly.
  hhds::SourceId                pending_srcid_{0};

public:
  static constexpr char version[] = "0.1.0";

  explicit Lnast() : Lnast("noname") {}
  explicit Lnast(std::string_view _module_name);
  // Wrap an already-built unattached tree (no Forest, no TreeIO). Used by
  // the upass runner to wrap a `Forest::create_tree_temp` body so the
  // existing add_child / set_data API drives staging emission. The wrapper
  // cannot be replaced (no TreeIO); replace_body() asserts on this Lnast.
  Lnast(std::shared_ptr<hhds::Tree> body, std::string_view _module_name);
  ~Lnast();

  // ── forest interchange (the lhd `ln:` directory = hhds::Forest::save) ───
  // Clone this Lnast's tree (attrs included) into `forest` as a tree named
  // by top_module_name, so N units can ride one Forest::save directory.
  void export_into(hhds::Forest& forest) const;
  // Wrap one tree of an externally loaded Forest (hhds::Forest::load). The
  // returned Lnast shares ownership of `forest`; replace_body() works (the
  // TreeIO is present). io_meta/bw_meta start empty — re-run the upasses.
  // Returns nullptr when `module_name` is not in the forest.
  static std::shared_ptr<Lnast> adopt(std::shared_ptr<hhds::Forest> forest, std::string_view module_name);

  // ── tree access ─────────────────────────────────────────────────────────
  hhds::Tree&                          tree() noexcept { return *tree_; }
  const hhds::Tree&                    tree() const noexcept { return *tree_; }
  std::shared_ptr<hhds::Tree>          tree_ptr() const noexcept { return tree_; }
  const std::shared_ptr<hhds::Forest>& forest() const noexcept { return forest_; }

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
  void                         set_name(const Lnast_nid& nid, std::string_view name);

  // ── source provenance ([[1f]]) ──────────────────────────────────────────
  // The old per-node `lnast.loc` struct + `lnast.fname` string attributes are
  // gone; one uint64 SourceId (hhds::attrs::srcid) resolved through the
  // locator below replaces both.
  hhds::Source_locator&       source_locator() noexcept { return srcloc_; }
  const hhds::Source_locator& source_locator() const noexcept { return srcloc_; }

  hhds::SourceId get_srcid(const Lnast_nid& nid) const;
  void           set_srcid(const Lnast_nid& nid, hhds::SourceId id);

  hhds::SourceId pending_srcid() const noexcept { return pending_srcid_; }
  void           set_pending_srcid(hhds::SourceId id) noexcept { pending_srcid_ = id; }

  // Def-bearing node kinds carry a SourceId; operand refs/consts and pure
  // structure (stmts, type subtrees) carry nothing — the provenance of an
  // operand is the provenance of its defining node.
  static bool srcid_carries(Lnast_ntype::Lnast_ntype_int t) {
    return !(Lnast_ntype::is_ref(t) || Lnast_ntype::is_const(t) || Lnast_ntype::is_invalid(t) || Lnast_ntype::is_stmts(t)
             || Lnast_ntype::is_type(t));
  }

  // Resolved diagnostic span / secondary anchors for a node, at emit time.
  // Null span / empty notes when the node carries no (resolvable) srcid.
  livehd::diag::Span              span_of(const Lnast_nid& nid) const;
  std::vector<livehd::diag::Note> notes_of(const Lnast_nid& nid, std::string_view message = "related source") const;

  // set_data: write-side helpers used by add_child / set_root /
  // append_sibling. On the read side, callers go through get_type/get_name.
  void set_data(const Lnast_nid& nid, Lnast_ntype::Lnast_ntype_int type);
  void set_data(const Lnast_nid& nid, const Lnast_node& n);

  // ── module metadata ─────────────────────────────────────────────────────
  std::string_view get_top_module_name() const { return top_module_name; }

  // ── lambda kind (Task 1r; stamped by func_extract on extracted trees) ───
  std::string_view get_lambda_kind() const noexcept { return lambda_kind_; }
  void             set_lambda_kind(std::string_view kind) { lambda_kind_ = kind; }

  // ── deferred template (Task 1p; stamped by func_extract; cleared on a
  //     specialized clone). True ⇒ no LGraph at definition time. ───────────
  bool is_template() const noexcept { return template_; }
  void set_template(bool t) noexcept { template_ = t; }
  // Generic type-parameter names (`<T, U>`), seam for the follow-up goal.
  void                            set_generics(std::vector<std::string> g) { generics_ = std::move(g); }
  bool                            has_generics() const noexcept { return !generics_.empty(); }

  // ── pub export list (Task 1m; recorded by prp2lnast on file-level trees) ─
  const std::vector<Lnast_pub_entry>& get_pub_list() const noexcept { return pub_list_; }
  void add_pub(std::string_view name, std::string_view kind, hhds::SourceId srcid = 0) {
    pub_list_.push_back({std::string(name), std::string(kind), srcid});
  }
  // Folded pub-value leaves (set by uPass_constprop at file-walk completion).
  const std::vector<std::pair<std::string, std::string>>& get_pub_values() const noexcept { return pub_values_; }
  void set_pub_values(std::vector<std::pair<std::string, std::string>> v) { pub_values_ = std::move(v); }

  // ── I/O metadata side-channel (set by the SSA upass when ssa:1) ─────────
  const Lnast_tree_io& io_meta() const noexcept { return io_meta_; }
  Lnast_tree_io&       io_meta() noexcept { return io_meta_; }

  // ── Bitwidth metadata side-channel (set by uPass_bitwidth::end_run) ──────
  const Lnast_bitwidth_meta& bw_meta() const noexcept { return bw_meta_; }
  Lnast_bitwidth_meta&       bw_meta() noexcept { return bw_meta_; }

  // ── name predicates (work off the textual name only) ────────────────────
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
