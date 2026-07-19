//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <memory>
#include <ostream>
#include <stack>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "lnast.hpp"

// Lnast_prp_writer — emits a Pyrope 3.0 source file from an LNAST.
//
// Standalone walker (does NOT subclass Lnast_writer because Lnast_writer's
// write_* methods are not virtual — the dispatch switch calls them directly
// by name, so subclass overrides are invisible to write_lnast()).
//
// Nodes not yet handled (func_def body, tuple flattening) are emitted with a
// /* TODO: <node-type> */ comment so the output stays parseable.
//
// Usage:
//   auto staging = runner.take_staging();
//   Lnast_prp_writer writer(std::cout, staging);
//   writer.write_all();
class Lnast_prp_writer {
public:
  // Takes lnast by value so the writer owns the tree for its entire lifetime
  // (avoids dangling-reference UB if the caller's shared_ptr is destroyed).
  explicit Lnast_prp_writer(std::ostream& os, std::shared_ptr<Lnast> lnast);
  void write_all();

  // Debug mode (pass option `prp_writer.debug=true`): when false (default) an
  // unimplemented construct is recorded so the pass turns it into a fatal
  // diagnostic (the compile must NOT silently succeed on a TODO-laden output);
  // when true only a `/* TODO */` comment is emitted so a developer can inspect
  // the partial output.  Either way the marker is written, the difference is
  // whether the surrounding compile is allowed to pass.
  void set_debug(bool d) { debug_ = d; }
  // True if write_all() emitted any /* TODO */ for an unimplemented construct.
  bool                            has_unimplemented() const { return !unimplemented_.empty(); }
  const std::vector<std::string>& unimplemented() const { return unimplemented_; }

  // The names of every module emitted in this run.  A func_call callee that is
  // one of them becomes a file-top `const X = import("X.X")` so the cross-module
  // call resolves on re-compile.  Owned by the pass; must outlive write_all().
  void set_known_modules(const std::unordered_set<std::string>* m) { known_modules_ = m; }
  // The names (last `.`-component) of every module emitted in this run.  A
  // func_call whose callee is one of these is a real submodule instantiation, so
  // the writer annotates it with `::[name=<lhs>]` to preserve the bound
  // variable's hierarchical instance name on re-compile (else tolg synthesises
  // `u_<callee>_…`, breaking name correspondence with the original v2prp source).
  // Covers stateless `comb`s too: with `upass.inline=false` they stay Sub
  // instances, and the annotation is inert when a comb is inlined.  Owned by the
  // pass; must outlive write_all().
  void set_instantiated_modules(const std::unordered_set<std::string>* m) { instantiated_modules_ = m; }

private:
  std::ostream&          os;
  std::shared_ptr<Lnast> lnast;
  int                    depth{0};
  bool                   debug_{false};
  // Human-readable descriptions of every unimplemented construct hit (one per
  // emitted /* TODO */); pass.prp_writer reads this to fail the compile.
  std::vector<std::string> unimplemented_;

  std::stack<Lnast_nid> nid_stack;
  Lnast_nid             cur;

  // ── Cursor helpers ───────────────────────────────────────────────────────
  bool                         move_to_child();
  bool                         move_to_sibling();
  void                         move_to_parent();
  bool                         is_last_child() const;
  std::string_view             current_text() const;
  Lnast_ntype::Lnast_ntype_int current_ntype() const;

  // ── Output helpers ───────────────────────────────────────────────────────
  void print(std::string_view s);
  void print_indent();
  void println(std::string_view s = "");

  // ── Main dispatch ────────────────────────────────────────────────────────
  void write_node();

  // Record an unimplemented construct in unimplemented_ and emit the parseable
  // `/* TODO: <what> */` marker inline at the cursor.
  void emit_unimplemented(std::string_view what);

  // ── Node writers ─────────────────────────────────────────────────────────
  void write_top();
  void write_module();  // slang-origin: io node + body -> comb|mod NAME(...) -> (...) { … }
  void write_stmts();
  void write_if();
  void write_declare();  // declare(ref, type, qualifier, [value])
  void write_store();    // store(var, level0..levelN, value)
  void write_ref();
  void write_const();
  void write_cassert();
  void write_func_call();
  void write_func_def();
  // for( value_ref, iterable_ref, stmts(body), const(mode) [, idx_ref [, key_ref]] )
  // -> `for <value> in [ref ]<iterable> { <body> }`, or with index/key binds
  // `for (<idx>, <value>[, <key>]) in …` (Pyrope binds the INDEX first).  A runtime
  // `for` only survives to the writer inside a generic/template lambda the
  // runner could not monomorphize (the comptime unroll handles every concrete
  // instantiation); re-emitting it keeps that template lambda parseable.
  void write_for();
  void write_tuple_add();
  // tuple_concat( dst, src0, src1, … ) -> `dst = (...src0, ...src1, …)` (spread
  // concatenation): each source tuple is splatted into one combined literal.
  void write_tuple_concat();
  // Renders a tuple_add node in EXPRESSION position (no LHS child) as a Pyrope
  // tuple literal `(v0, v1, …)` — used for a memory declare's initializer.
  void write_tuple_literal();
  void write_attr_set();
  void write_delay_assign();

  // Statement form of a value-producing op (`lhs = <rhs>`).  The RHS itself is
  // rendered by render_def_rhs(), which also inlines single-use temps.  Used for
  // every infix/unary/postfix value op (plus, bit_and, sext, get_mask,
  // tuple_get, attr_get, …) — they differ only in how render_def_rhs spells the
  // RHS, so the statement wrapper is shared.
  void write_value_stmt();
  // range( dst, lo, hi ) fallback — a range temp not folded into a get_mask
  // (the usual consumer) emits `dst = lo..=hi`.
  void write_range();
  // set_mask — emit a `dst#[lo..=hi] = ins` bit-range LHS assign (RMW)
  void write_set_mask();
  // type_spec( ref(var), type ) is a bare type assertion the runner emits for
  // inlined-call temps.  Its type is folded into the variable's first
  // declaration (write_store), so the standalone statement emits nothing.
  void write_type_spec();

  // ── Pipeline (stage[N] / @[N]) ───────────────────────────────────────────
  // Format a stages(min,max) node into a Pyrope cycle-annotation body: "N"
  // when min==max, "A..=B" otherwise, "" for the unconstrained bare-pipe (1,0)
  // sentinel (renders as `@[]`).  Cursor-independent.
  std::string format_stages(Lnast_nid stages_nid) const;
  // The first `stages` child of a node (io-port store / pipe declare), or an
  // invalid nid when there is none.
  Lnast_nid   find_stages_child(Lnast_nid nid) const;
  // A statement that write_node() renders to nothing — a `type_spec` (folded
  // into a declaration) or a stage `declare` (re-attached to its store as
  // `stage[N] x = v`).  Skipped by the body emit loops so no blank line is left.
  bool        emits_nothing_stmt(Lnast_nid nid) const;
  // Per-variable pipeline depth recorded by a `reg` declare carrying a trailing
  // stages node (the `stage[N] x = v` lowering): the next store to the var
  // emits `stage[<depth>] x = v`; the bare declare itself is suppressed.
  std::unordered_map<std::string, std::string> stage_decls_;
  // Per-variable type recorded by a `type_spec` statement, folded into the
  // variable's first declaration (`mut x:T = v`).
  std::unordered_map<std::string, std::string> type_specs_;

  // Serialises a type node (cursor must sit on the type child) into a Pyrope
  // type suffix without moving the cursor: "" for prim_type_none, "bool",
  // "string", or "int"/"uN"/"sN" reconstructed from a prim_type_int range.
  std::string render_type();
  // Same, but on an explicit node id (used by the io-port walk, which navigates
  // the tree directly rather than through the shared cursor). Also handles
  // comp_type_array -> "[N]T".
  std::string render_type_at(Lnast_nid type_nid);

  // Emits the `comb|mod NAME(in:T, …) -> (out:T, …)` header from the io node
  // (cursor-independent; reads the io subtree via direct tree accessors).
  void        emit_module_header(Lnast_nid io_nid, bool is_mod);
  // Emits one `(p0:T0, p1:T1, …)` parenthesised port list from a `tuple_add`
  // of `store(ref(name), const(init|nil), [type], [stages])` entries.  Shared by
  // emit_module_header (slang io node) and write_func_def (pyrope lambda
  // signature).  is_output adds the `@[N]` landing-cycle annotation on a `mod`.
  void        emit_port_group(Lnast_nid tup_nid, bool is_output, bool is_mod);
  // True if the module body declares state (a `reg`/`latch` declare, anywhere
  // in the stmts subtree) — selects `mod` over `comb`.
  bool        body_has_state(Lnast_nid stmts_nid) const;
  // The lambda name to emit: the last `.`-component of the top module name
  // (e.g. "trivial_if.fun3" -> "fun3"), so the generated identifier is a plain
  // Pyrope name (no dotted/escaped identifier the re-compile leg would reject).
  std::string lambda_name() const;

  // ── Declaration tracking ─────────────────────────────────────────────────
  // Maps a variable name to its pending storage-class keyword ("mut", "reg",
  // "wire") recorded when write_attr_set() suppresses an attr_set x type kw
  // node.  The NEXT assignment to that variable consumes the keyword (once).
  std::unordered_map<std::string, std::string> pending_decl_;

  // Returns the stored keyword for `lhs` (e.g. "mut") and removes it from
  // the map, or returns "" if no pending declaration exists.
  std::string take_decl_keyword(std::string_view lhs);

  // Names already introduced in the current lambda (io ports, explicit
  // `declare` nodes, and prior first-writes).  Post-upass slang LNAST writes to
  // SSA-renamed user variables (`a___ssa_1`) and bare wires with no `declare`
  // node; Pyrope rejects assignment to an undeclared variable, so the first
  // write to such a name must carry a `mut`.  `___`-prefixed compiler temps
  // auto-declare and are never tracked.
  std::unordered_set<std::string> declared_;
  // Vars whose nested `mut` declare was hoisted to a `mut X = 0` at the function
  // top (see emit_module): write_declare drops the in-place nested declare.
  std::unordered_set<std::string> suppress_decl_;
  // Store-driven body nets that need NO hoist: the name has exactly ONE definition
  // in the whole body, that definition is a top-level `store`, and nothing reads it
  // before that store.  Its declaration rides on the store itself as an in-place
  // `const X = <rhs>` (decl_prefix), instead of a `mut X = 0` prologue line plus a
  // far-away re-bind.  See the eligibility scan in emit_module.
  std::unordered_set<std::string> single_store_;
  // Single-store nets whose value is an imported-package comptime const
  // (`x = pkg.PARAM`). Declared `mut` (not `const`): a `const` bound to a
  // comptime value BECOMES comptime, and copying it into a conditionally-driven
  // net (a mux target) makes that net look comptime too → "const rebind" on
  // recompile. `mut` keeps the value runtime and breaks the cascade.
  std::unordered_set<std::string> pkg_valued_store_;
  // `wire` net names that have a real-statement store driver somewhere in the
  // body (populated by write_module's pre-scan).  A `wire` is a single-driver
  // net, so write_declare must NOT add the combinational `= 0` default to such a
  // wire — the store is its sole driver and a `= 0` would make it multi-driven.
  std::unordered_set<std::string> wire_stored_;
  // Bundle reconstruction: a base name (`io`) -> its leaf field set (`operation`,
  // `inputx`, …). upass.detuple split a scalar tuple `wire io:(...)` into dotted
  // leaf nets (`io.operation`); the writer regroups them back into ONE
  // `wire io:(operation:u5, …)` declaration and renders every `io.field` access as
  // the bare dotted path (not an escaped `` `io.field` ``), so the bundle/struct
  // info surfaces in the emitted Pyrope. On recompile detuple re-splits it.
  std::unordered_map<std::string, std::unordered_set<std::string>> bundle_fields_;
  // True if `name` is a known bundle field `base.field` (rendered unescaped).
  bool is_bundle_field(std::string_view name) const;
  bool is_imported_pkg_path(std::string_view name) const;  // `pkg.PARAM` on an imported package
  bool is_imported_package_name(std::string_view name) const;  // bare `pkg` is an imported package

  // Set of all module names emitted in this run (see set_known_modules); a
  // func_call callee in this set is emitted as a file-top import.
  const std::unordered_set<std::string>* known_modules_{nullptr};
  // Set of every module name emitted in this run, tail-keyed (see
  // set_instantiated_modules); a func_call to one of these is a real submodule
  // instantiation and the writer emits a `::[name=<lhs>]` call-site
  // instance-name annotation.
  const std::unordered_set<std::string>* instantiated_modules_{nullptr};
  // Distinct func_call callee names seen in this unit (populated in scan_node).
  std::unordered_set<std::string> func_call_callees_;
  // Import-const alias per callee module name, used when the natural import name
  // (`const X = import("X.X")`) would EXACTLY collide with a submodule instance
  // variable of the same spelling.  Names are case-sensitive, so the firtool
  // camelCase instance `subModule` does NOT collide with the import const
  // `SubModule`.  Maps module name -> emitted alias (== module name when no
  // collision).  Cleared per module.
  std::unordered_map<std::string, std::string>                     import_alias_;

  // Storage-class prefix to print before an assignment LHS: a pending
  // attr_set-type keyword if one is queued, else "mut " on the first write to
  // an untracked non-temp name, else "" (already declared, or a temp).  Marks
  // the name declared as a side effect.
  std::string decl_prefix(std::string_view lhs);

  // Reg/memory flop attributes the slang reader emits as standalone `attr_set`
  // statements (`r.[initial]=N`, `r.[reset_pin]=rst`, `data.[fwd]=0`, …).
  // Pyrope only accepts attribute writes folded into the DECLARATION
  // (`reg r:T:[init=N, reset_pin=rst]`), so write_module pre-collects them here
  // keyed by variable name (assembled "k=v, k=v" body) and write_declare emits
  // the `:[…]` suffix; the standalone attr_set statements are then skipped.
  std::unordered_map<std::string, std::string> folded_attrs_;
  // Per (var,attr) pairs folded above (key "var\x01attr"), so write_attr_set
  // skips re-emitting a folded attr that occurs deeper than the top-level body
  // (e.g. a memory's `mem.[wensize]=N` written inside the always block).
  std::unordered_set<std::string>              folded_keys_;
  // Body nets a reg binds as its clock/reset pin (`reg q:[clock_pin=ref <net>]`).
  // A derived clock (`gclk = clk_b & gate`) is an internal combinational signal:
  // the declare-first hoist would emit the reg ahead of `<net>`'s driver, so the
  // `ref` would resolve to the net's pre-driver value (the `clock_pin '0'`
  // tolg error). write_module emits each such net's driver BEFORE the reg
  // declares and skips its in-body statement; the names are stripped (post-SSA).
  std::unordered_set<std::string>              pin_dep_nets_;
  // EVERY body net read as the ref value of a folded attribute, for ANY key (a
  // superset of pin_dep_nets_, which covers only the `_pin` keys whose driver is
  // relocated ahead of the declares).  A folded attr rides on its variable's
  // declare, and declares are emitted before every body write — so such a net is
  // read early and cannot be declared in place by its store (single_store_).
  std::unordered_set<std::string>              folded_attr_refs_;
  // Transitive closure of pin_dep_nets_ over body-var operands: a derived clock
  // can read ANOTHER internal wire (`inv = ~gate; gclk = clk_b & inv`), and that
  // wire's driver must be relocated ahead of the declares too — otherwise the
  // relocated `gclk` driver would read `inv` while it is still the hoisted 0
  // (a silent dead clock).  Emitted in body order (combinational SSA defs
  // precede their uses), so dependencies land before their consumers.
  std::unordered_set<std::string>              pin_cone_;
  // Collect the body-variable names a defining statement READS (operands after
  // child0), following single-use folded temps into their definitions so a
  // `gclk = clk_b & inv` whose `& ` is an inlined temp still reports `inv`.
  void collect_driver_reads(Lnast_nid def_node, std::unordered_set<std::string>& out) const;
  // Same fold-following read collection applied to `node` ITSELF (a condition
  // ref, an if arm, …) rather than a defining statement's operand tail.
  void collect_node_reads(Lnast_nid node, std::unordered_set<std::string>& out) const;

  // Walk the top-level body statements and populate folded_attrs_ (mapping the
  // slang attr vocabulary to the Pyrope source one: initial->init,
  // sync->async with the value inverted, everything else verbatim).
  void        collect_folded_attrs(Lnast_nid stmts_nid);
  // Render an attr value leaf (const text or ref name) to Pyrope source.
  std::string render_attr_value(Lnast_nid value_nid) const;

  // ── Single-use temp folding (expression inlining) ─────────────────────────
  // The post-uPass LNAST is fully flattened: every operation is its own
  // statement assigning to a `___tmp`, so `res = a + 1` arrives as
  // `plus(___t, a, 1)` + `store(res, ___t)`.  Folding inlines a `___tmp` that
  // is written once and read once back into its single use, undoing the
  // flattening so the emitted Pyrope reads like the source (`res = a + 1`).
  struct Fold_info {
    Lnast_nid                    def_node;
    Lnast_ntype::Lnast_ntype_int def_type  = Lnast_ntype::Lnast_ntype_invalid;
    int                          def_count = 0;   // assignments to this name
    int                          use_count = 0;   // reads of this name
    int                          def_index = -1;  // pre-order index of the (single) def
    int                          use_index = -1;  // pre-order index of the (single) use
    int                          min_use_index = 1 << 30;  // pre-order index of the FIRST (earliest) use
  };
  std::unordered_map<std::string, Fold_info>                           fold_info_;    // by raw name
  std::unordered_map<std::string, std::vector<int>>                    write_idx_;    // name -> sorted write pre-order indices
  // name -> EXCLUSIVE end pre-order index of the func_call subtree that defines it
  // (one past the last index used by that statement, i.e. by the result var, the
  // callee, and EVERY argument expression). A read whose index falls inside
  // [def_index, end_index) is a self-reference from within the instantiation's
  // OWN argument list (e.g. a handshake port wired to the instance's own output,
  // `inst(... , ready_i = inst.valid_o)`) — analyze_instance_inline's try_inline
  // uses this to treat it as genuine feedback (never inlined), not merely a def
  // BEFORE the call's first node (which `inst_def_index` alone cannot detect,
  // since every argument is itself a descendant of the call and so sorts AFTER
  // the call's own start index).
  std::unordered_map<std::string, int>                                 func_call_end_idx_;
  std::unordered_set<std::string>                                      foldable_;     // temp names to inline at their use
  std::unordered_set<int64_t>                                          folded_node_;  // def-node keys (get_class_index) to skip
  std::unordered_set<std::string>                                      dead_signals_; // stripped names written but never read (dropped)
  // Every READ name plus its dotted ancestor prefixes (`a.b.c` read -> {a, a.b,
  // a.b.c}). Lets the bundle reconstruction skip re-grouping a NEVER-read tuple
  // base into `wire base:(...) = nil` — detuple only re-splits a tuple that is
  // read, so a write-only bundle would leave field stores tolg cannot lower.
  std::unordered_set<std::string>                                      read_field_prefixes_;
  // Mark/collect signals that are assigned but never read (and are not ports,
  // regs, the clock/reset cone, or instance temps) so their def statements are
  // dropped. firtool SSA+poison-init emits a dead base per versioned signal.
  void compute_dead_signals(Lnast_nid io_nid, Lnast_nid stmts_nid);

  // ── mux collapse ──────────────────────────────────────────────────────────
  // An if/unique-if whose every arm is a single value-def to the SAME scalar `x`
  // is a mux: `x=D; if c {x=v}` (Verilog `x = c ? v : D`). Collapse it to one
  // conditional-expression assignment `x = if c0 {v0} elif c1 {v1} … else {D}`,
  // matching the RTL's size AND data-flow complexity. An n-way unique-if (Verilog
  // parallel/`unique case` -> hotmux) becomes the if/elif chain.
  struct Mux_arm {
    Lnast_nid cond;  // condition ref/const
    Lnast_nid def;   // the arm's single value-def stmt (render_def_rhs gives the value)
  };
  struct Mux_info {
    std::string          lhs;             // target scalar (stripped)
    bool                 unique = false;  // was a unique-if
    std::vector<Mux_arm> arms;
    Lnast_nid            else_def;        // else value-def: the else arm, or the preceding default store
    bool                 fold_decl = false;  // emit `mut lhs[:T] = if…` (the poison `mut lhs = 0` declare was dropped)
    std::string          decl_type;          // type suffix for the folded declare (may be empty)
  };
  std::unordered_map<int64_t, Mux_info> mux_info_;  // keyed by if-node class index
  void                                  analyze_muxes(Lnast_nid stmts_nid);
  // The single value-def stmt of a stmts block that writes ONLY scalar `out_lhs`
  // (returns its node), or invalid. If `expect` is non-empty the target must equal
  // it; otherwise the target is reported in out_lhs.
  Lnast_nid arm_value_def(Lnast_nid stmts_node, std::string expect, std::string& out_lhs) const;
  std::unordered_map<std::string, std::pair<std::string, std::string>> range_lohi_;   // range-temp name -> "lo","hi"
  std::vector<Lnast_nid> get_mask_nodes_;                                             // every get_mask, for range-mask resolution
  std::vector<std::pair<Lnast_nid, int>> tuple_get_nodes_;                            // every tuple_get + its pre-order index
  std::vector<std::pair<Lnast_nid, int>> store_nodes_;                                // every store + its pre-order index
  // Module-instance results (`mut inst = Mod(args)`), stripped names: their output
  // ports may print with dot notation `inst.port` (instead of `inst["port"]`).
  std::unordered_set<std::string> instance_results_;
  // Instance-output extraction temps (`_t = inst["port"]`) selected to be inlined
  // as `inst.port` at every use; their hoisted `wire`/`mut` declaration is dropped.
  std::unordered_set<std::string> instance_output_inlined_;

  // Pre-pass: walk the whole tree, populate the maps above, and decide which
  // temps are foldable.  Called once at the start of write_all().
  void        analyze_folding();
  void        scan_node(Lnast_nid nid, int& index);  // recursive pre-order populate
  // Decide which submodule output-port reads (`_t = inst["port"]`) to inline as
  // `inst.port` at their uses (backward-only), vs keep as a `wire` (a use that
  // precedes the instance declaration — genuine pipeline feedback).  Runs inside
  // write_module after the clock/reset pin cone is known.
  void        analyze_instance_inline();
  // True if a node type writes its child0 (an LHS def).  if/cassert/loop and the
  // pseudo-func_* nodes read child0 instead, so they return false.
  static bool defines_child0(Lnast_ntype::Lnast_ntype_int t);
  // A `store(lhs, value)` with no index levels and a ref/const value — the only
  // store shape that inlines as a plain copy.
  bool        is_pure_copy(Lnast_nid store_node) const;
  // No operand of `def_node` is reassigned strictly between pre-order indices
  // (d, u) — the condition that makes moving a pure expression to its single use
  // value-preserving.
  bool        operands_stable(Lnast_nid def_node, int d, int u) const;
  // True if `name` is a `___tmp` selected for inlining.
  bool        is_foldable(std::string_view name) const { return foldable_.count(std::string(name)) != 0; }
  // True if a node's def-key is in folded_node_ (its statement is suppressed).
  bool        is_folded_node(Lnast_nid nid) const { return folded_node_.count(nid.get_class_index().value) != 0; }

  // Render a value node (ref/const, or a foldable temp's inlined expression) to
  // a Pyrope expression string.  operand_ctx => parenthesise a loose (infix /
  // unary) sub-expression so precedence is preserved when it nests inside
  // another operator; tight postfix forms (`x#[..]`, `x[i]`, `x.[a]`) never get
  // wrapped.
  std::string render_value(Lnast_nid node, bool operand_ctx);
  // Render the RHS expression (everything after `lhs = `) of a value-producing
  // "defining" node, inlining folded operands recursively.
  std::string render_def_rhs(Lnast_nid def_node, bool operand_ctx);
  // A const leaf -> its Pyrope spelling (number / true|false|nil / quoted string).
  std::string const_text(Lnast_nid node) const;

  // ── Utilities ────────────────────────────────────────────────────────────
  // True for a compiler SSA temp: a raw `%`-prefixed name (or legacy `___`
  // during the migration), OR a name this writer already mapped to an emittable
  // `t…` form (so post-strip_prefix call sites still recognise it as a temp).
  bool                    is_tmp(std::string_view name) const;
  // The infix symbol for a binary/n-ary op ntype ("+", "==", "and", …) or "" if
  // the type is not an infix op.
  static std::string_view infix_symbol(Lnast_ntype::Lnast_ntype_int t);
  // True if `t` is a value-producing op whose single-use temp can be inlined.
  static bool             is_foldable_optype(Lnast_ntype::Lnast_ntype_int t);
  std::string             strip_prefix(std::string_view name) const;  // renames ___ssa_N out, maps %temp -> emittable

  // ── %temp -> emittable Pyrope identifier ─────────────────────────────────
  // A `%`-prefixed compiler temp is parser-impossible, so a non-inlined temp
  // that must be EMITTED as source is renamed to a legal `t<suffix>` identifier
  // (`%pipe_o` -> `tpipe_o`, `%3439_0` -> `t3439_0`), collision-checked against
  // every name in the tree (and previously synthesised names) so it can never
  // alias a user variable / port: on collision the suffix `_<M>` is appended
  // until free.  Stable (the `%` suffix derives from the content hash) and
  // consistent (def and use share the cached mapping).
  std::string emit_name_for(std::string_view tmp) const;
  mutable std::unordered_map<std::string, std::string> tmp_emit_names_;     // %head -> t<id>
  mutable std::unordered_set<std::string>              used_emit_names_;    // every output name taken
  mutable std::unordered_set<std::string>              emitted_tmp_names_;  // mapped t<id> that ARE temps
  mutable bool                                         emit_names_seeded_{false};
};
