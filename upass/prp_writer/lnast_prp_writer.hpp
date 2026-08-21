//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <algorithm>
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

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
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

  // ── group emit (N units of ONE source file into one .prp) ─────────────────
  // pass.prp_writer writes every unit that came from the same source file into
  // that file's `<file>.prp` (a file-level unit plus its `pub mod` lambdas), so
  // the emitted tree mirrors the input tree instead of exploding into
  // `<file>.prp` + `<file>.<entity>.prp`. The file-scope `const X = import(…)`
  // header must then be hoisted above ALL of them and deduped: point every
  // writer of the group at one sink, call collect_header() on each, print the
  // sink, then write_all() the bodies (which now skip their own header).
  void set_header_sink(std::vector<std::string>* sink) { header_sink_ = sink; }
  void collect_header();

  // Debug mode (pass option `prp_writer.debug=true`): when false (default) an
  // unimplemented construct is recorded so the pass turns it into a fatal
  // diagnostic (the compile must NOT silently succeed on a TODO-laden output);
  // when true only a `/* TODO */` comment is emitted so a developer can inspect
  // the partial output.  Either way the marker is written, the difference is
  // whether the surrounding compile is allowed to pass.
  void                            set_debug(bool d) { debug_ = d; }
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
  std::ostream&             os;
  std::shared_ptr<Lnast>    lnast;
  int                       depth{0};
  bool                      debug_{false};
  // Human-readable descriptions of every unimplemented construct hit (one per
  // emitted /* TODO */); pass.prp_writer reads this to fail the compile.
  std::vector<std::string>  unimplemented_;
  // Group emit (see set_header_sink): where the file-scope import lines go, and
  // whether they have been produced already (write_module_imports is
  // idempotent so collect_header + write_all cannot emit them twice).
  std::vector<std::string>* header_sink_{nullptr};
  bool                      header_done_{false};
  bool                      prepared_{false};

  std::stack<Lnast_nid> nid_stack;
  Lnast_nid             cur;

  // One-time pre-walk (fold analysis + timecheck index + file-import scan)
  // shared by collect_header() and write_all().
  void prepare();

  // ── file-scope import residue (Pyrope-origin file-level units) ────────────
  // See scan_file_imports: the elaborated tree keeps a folded skeleton of each
  // `const X = import("path")`; these sets let write_stmts drop it and the
  // header re-emit the binding.
  void                             scan_file_imports();
  // Dead initial stores (`X = <const>` immediately superseded, no read between):
  // dropped from the emission AND excluded from the fold analysis, so the
  // surviving def counts as single-use and can inline at its reader.
  void                             scan_dead_init_stores();
  Lnast_nid                        body_stmts_nid() const;
  absl::flat_hash_set<int64_t>     dead_init_stmts_;
  // Names a NESTED store writes to. Their top-level declaration/seed is what
  // that store binds against, so neither statement drop may remove it.
  void                             scan_nested_defs();
  absl::flat_hash_set<std::string> nested_def_names_;
  bool                             drops_as_import_residue(Lnast_nid nid) const;
  bool                             is_pub_export(std::string_view name) const;
  bool                             is_declare_with_value(Lnast_nid nid) const;
  // Re-nest a struct local the front end flattened into escaped per-field leaves
  // (`` `sig.cmd` ``, `` `sig.txfma` ``, …) back into one `mut sig = (mut cmd =
  // 0, …)`. Fills body_bundle_text_/skip_ (keyed by the leaf declare's class
  // index) plus bundle_fields_/declared_, exactly like collect_port_groups.
  void                             collect_body_bundles(Lnast_nid body_nid);
  // The rebuilt literal already zeroes each field, so the front end's flat
  // `base.field = 0` seed writes 0 over 0 — drop it while the field is still
  // known zero.
  void                             drop_redundant_bundle_zeros(Lnast_nid body_nid, const std::vector<std::string>& zero_fields);
  absl::flat_hash_map<int64_t, std::string>        body_bundle_text_;
  absl::flat_hash_set<int64_t>                     body_bundle_skip_;
  bool                                             file_level_{false};
  absl::flat_hash_set<std::string>                 import_tmps_;   // %tmp an import() fcall wrote
  absl::flat_hash_set<std::string>                 import_bound_;  // file-scope name bound to one
  absl::flat_hash_set<std::string>                 file_stored_;   // file-scope names with a real store
  // Compiler temps written MORE THAN ONCE (a lane write counts): they must be
  // declared `mut`, not `const`, and — when the writes straddle sibling scopes
  // — seeded at the function top. Filled by the body hoist scan.
  absl::flat_hash_set<std::string>                 multi_def_tmp_;
  // Non-declare definition count per body name, from the same scan.
  absl::flat_hash_map<std::string, int>            def_count_;
  // FIRST top-level def / read index per body name, from the same scan. A
  // declaration whose def precedes every read is redundant — the def declares it.
  absl::flat_hash_map<std::string, size_t>         def_idx_;
  absl::flat_hash_map<std::string, size_t>         read_idx_;
  std::vector<std::pair<std::string, std::string>> file_imports_;  // (alias, import path), source order

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
  void      write_top();
  // Emit file-scope imports for sibling modules called by this unit. Shared by
  // slang-origin io/body units and Pyrope-origin func_def units.
  void      write_module_imports();
  void      write_module();  // slang-origin: io node + body -> comb|mod NAME(...) -> (...) { … }
  void      write_stmts();
  void      write_if();
  // The body of write_if. `continuation` renders this if-node as an ` elif …`
  // continuing the caller's chain (else-flattening) instead of opening one.
  void      write_if_chain(bool continuation);
  // A `unique if` chain whose every condition is `<scrutinee> == K` (or an
  // `or` of such, K a constant / imported pkg.PARAM) is an SV case statement —
  // render it as `match <scrutinee> { == K {…} in (K1, K2) {…} else {…} }`.
  // Prints and returns true (cursor back on the if node), or false to fall
  // through to the plain chain rendering.
  bool      try_write_match();
  // The single REAL statement of an else-stmts block when it is a PLAIN
  // (non-unique, non-mux-collapsed) if — the `else { if … }` nesting the reader
  // produces for SV `else if` ladders, flattened to ` elif ` by write_if_chain.
  // Invalid when the block holds anything else.
  Lnast_nid flattenable_nested_if(Lnast_nid stmts_nid) const;
  void      write_declare();  // declare(ref, type, qualifier, [value])
  void      write_store();    // store(var, level0..levelN, value)
  void      write_ref();
  void      write_const();
  void      write_cassert();
  void      write_func_call();
  void      write_func_def();
  // for( value_ref, iterable_ref, stmts(body), const(mode) [, idx_ref [, key_ref]] )
  // -> `for <value> in [ref ]<iterable> { <body> }`, or with index/key binds
  // `for (<idx>, <value>[, <key>]) in …` (Pyrope binds the INDEX first).  A runtime
  // `for` only survives to the writer inside a generic/template lambda the
  // runner could not monomorphize (the comptime unroll handles every concrete
  // instantiation); re-emitting it keeps that template lambda parseable.
  void      write_for();
  // Explicit compact-loop transport: emit its surviving source domain/body;
  // the final hidden stmts child is for tolg only.
  void      write_rolled_for();
  void      write_tuple_add();
  // tuple_concat( dst, src0, src1, … ) -> `dst = (...src0, ...src1, …)` (spread
  // concatenation): each source tuple is splatted into one combined literal.
  void      write_tuple_concat();
  // Renders a tuple_add node in EXPRESSION position (no LHS child) as a Pyrope
  // tuple literal `(v0, v1, …)` — used for a memory declare's initializer.
  void      write_tuple_literal();
  void      write_attr_set();
  void      write_delay_assign();

  // Statement form of a value-producing op (`lhs = <rhs>`).  The RHS itself is
  // rendered by render_def_rhs(), which also inlines single-use temps.  Used for
  // every infix/unary/postfix value op (plus, bit_and, sext, get_mask,
  // tuple_get, attr_get, …) and for the call-shaped `concat(a, b, c)` — they
  // differ only in how render_def_rhs spells the RHS, so the statement wrapper
  // is shared.
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
  std::string                                   format_stages(Lnast_nid stages_nid) const;
  // The first `stages` child of a node (io-port store / pipe declare), or an
  // invalid nid when there is none.
  Lnast_nid                                     find_stages_child(Lnast_nid nid) const;
  // A statement that write_node() renders to nothing — a `type_spec` (folded
  // into a declaration) or a stage `declare` (re-attached to its store as
  // `stage[N] x = v`).  Skipped by the body emit loops so no blank line is left.
  bool                                          emits_nothing_stmt(Lnast_nid nid) const;
  // Per-variable pipeline depth recorded by a `reg` declare carrying a trailing
  // stages node (the `stage[N] x = v` lowering): the next store to the var
  // emits `stage[<depth>] x = v`; the bare declare itself is suppressed.
  absl::flat_hash_map<std::string, std::string> stage_decls_;
  // Per-variable type recorded by a `type_spec` statement, folded into the
  // variable's first declaration (`mut x:T = v`).
  absl::flat_hash_map<std::string, std::string> type_specs_;
  // 1-D declared array sizes (`x:[N]T`, any mode): write_store expands a whole
  // array-to-array copy (`d = q`) into per-element stores (the recompile has
  // no lowering for a multi-element store between memories).
  absl::flat_hash_map<std::string, int64_t>     array_decl_size_;
  // The reader lowers `lhs = rhs@[N]` into a timecheck statement followed by
  // the store.  The immutable LNAST is indexed once so write_store never walks
  // all preceding siblings to rediscover the matching timecheck.
  absl::flat_hash_map<int64_t, Lnast_nid>       store_timechecks_;
  void                                          index_store_timechecks();
  std::string                                   render_timecheck_suffix(Lnast_nid check) const;

  // Serialises a type node (cursor must sit on the type child) into a Pyrope
  // type suffix without moving the cursor: "" for prim_type_none, "bool",
  // "string", or "int"/"uN"/"sN" reconstructed from a prim_type_int range.
  std::string                           render_type();
  // Same, but on an explicit node id (used by the io-port walk, which navigates
  // the tree directly rather than through the shared cursor). Also handles
  // comp_type_array -> "[N]T".
  std::string                           render_type_at(Lnast_nid type_nid);
  // Scalar UNSIGNED widths (`a_i:u52` -> 52) of ports (recorded as the signature
  // prints) and of `uN`-declared body variables (recorded by the pre-walk), so a
  // mask that selects every bit of one can be dropped as a no-op. Only `uN`: on
  // a signed `sN` the same mask REINTERPRETS the value, which is a real op.
  absl::flat_hash_map<std::string, int> port_bits_;
  void                                  note_port_width(std::string_view name, std::string_view type_txt);
  bool                                  is_whole_width_mask(Lnast_nid src, int lo, int hi) const;
  static std::string                    fmt_bit_range(std::string_view s, int lo, int hi);
  // The width of a value node's UNSIGNED range ([0, 2^w-1]) when the writer can
  // prove one, else nullopt. Lets the emitter drop a mask / `unsigned()` that
  // provably cannot change the value.
  std::optional<int>                    known_unsigned_bits(Lnast_nid n, int walk_depth = 0) const;
  bool                                  fits_unsigned_bits(Lnast_nid n, int64_t bits) const {
    auto w = known_unsigned_bits(n);
    return w && *w <= bits;
  }
  // A concat lane that is a non-negative integer literal fitting its window:
  // its own spelling (the window mask cannot change it). "0" for a zero lane,
  // which the caller drops from the OR tree entirely.
  std::optional<std::string> const_lane_value(Lnast_nid n, int64_t bits) const;
  // A declare initializer built only from compile-time constants (`5`, `(1, 2)`)
  // — the only kind the nested-mut hoist may RELOCATE to the function top.
  bool                       is_comptime_init(Lnast_nid n) const;
  std::string                render_comptime_init(Lnast_nid n);

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
  absl::flat_hash_map<std::string, std::string> pending_decl_;

  // Returns the stored keyword for `lhs` (e.g. "mut") and removes it from
  // the map, or returns "" if no pending declaration exists.
  std::string take_decl_keyword(std::string_view lhs);

  // Names already introduced in the current lambda (io ports, explicit
  // `declare` nodes, and prior first-writes).  Post-upass slang LNAST writes to
  // SSA-renamed user variables (`a___ssa_1`) and bare wires with no `declare`
  // node; Pyrope rejects assignment to an undeclared variable, so the first
  // write to such a name must carry a `mut`.  `___`-prefixed compiler temps
  // auto-declare and are never tracked.
  absl::flat_hash_set<std::string>                                   declared_;
  // Vars whose nested `mut` declare was hoisted to a `mut X = 0` at the function
  // top (see emit_module): write_declare drops the in-place nested declare.
  absl::flat_hash_set<std::string>                                   suppress_decl_;
  // Store-driven body nets that need NO hoist: the name has exactly ONE definition
  // in the whole body, that definition is a top-level `store`, and nothing reads it
  // before that store.  Its declaration rides on the store itself as an in-place
  // `const X = <rhs>` (decl_prefix), instead of a `mut X = 0` prologue line plus a
  // far-away re-bind.  See the eligibility scan in emit_module.
  absl::flat_hash_set<std::string>                                   single_store_;
  // Single-store nets whose value is an imported-package comptime const
  // (`x = pkg.PARAM`). Declared `mut` (not `const`): a `const` bound to a
  // comptime value BECOMES comptime, and copying it into a conditionally-driven
  // net (a mux target) makes that net look comptime too → "const rebind" on
  // recompile. `mut` keeps the value runtime and breaks the cascade.
  absl::flat_hash_set<std::string>                                   pkg_valued_store_;
  // `wire` net names that have a real-statement store driver somewhere in the
  // body (populated by write_module's pre-scan).  A `wire` is a single-driver
  // net, so write_declare must NOT add the combinational `= 0` default to such a
  // wire — the store is its sole driver and a `= 0` would make it multi-driven.
  absl::flat_hash_set<std::string>                                   wire_stored_;
  // Bundle reconstruction: a base name (`io`) -> its leaf field set (`operation`,
  // `inputx`, …). upass.detuple split a scalar tuple `wire io:(...)` into dotted
  // leaf nets (`io.operation`); the writer regroups them back into ONE
  // `wire io:(operation:u5, …)` declaration and renders every `io.field` access as
  // the bare dotted path (not an escaped `` `io.field` ``), so the bundle/struct
  // info surfaces in the emitted Pyrope. On recompile detuple re-splits it.
  absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>> bundle_fields_;
  // ── io tuple-port regrouping (write_module pre-header scan) ──────────────
  // upass.ssa flattened a tuple-typed PORT (`d:(x:u3, y:u5)`) into dotted leaf
  // io entries (`d.x`, `d.y` — one store per leaf under the io tuple_add).
  // collect_port_groups() regroups them: the header prints ONE entry per base
  // (`d:(x:u3, y:u5)`, multi-level leaves re-nested) and bundle_fields_ makes
  // every body access print the bare dotted path (`d.x`), so the emitted .prp
  // recompiles (ssa re-flattens) to the IDENTICAL per-leaf lgraph interface.
  // port_group_text_: FIRST-leaf io store nid -> the full rendered header
  // entry (a mod output's single trailing `@[…]` included); port_group_skip_:
  // the base's later leaf stores (already covered by the group entry). Keyed
  // by nid so write_func_def's port lists (never scanned) keep the per-leaf
  // printing unchanged.
  absl::flat_hash_map<int64_t, std::string>                          port_group_text_;
  absl::flat_hash_set<int64_t>                                       port_group_skip_;
  void                                                               collect_port_groups(Lnast_nid io_nid, bool is_mod);
  // True if `name` is a known bundle field `base.field` (rendered unescaped).
  bool                                                               is_bundle_field(std::string_view name) const;
  bool is_imported_pkg_path(std::string_view name) const;      // `pkg.PARAM` on an imported package
  bool is_imported_package_name(std::string_view name) const;  // bare `pkg` is an imported package

  // Set of all module names emitted in this run (see set_known_modules); a
  // func_call callee in this set is emitted as a file-top import.
  const std::unordered_set<std::string>*        known_modules_{nullptr};
  // Set of every module name emitted in this run, tail-keyed (see
  // set_instantiated_modules); a func_call to one of these is a real submodule
  // instantiation and the writer emits a `::[name=<lhs>]` call-site
  // instance-name annotation.
  const std::unordered_set<std::string>*        instantiated_modules_{nullptr};
  // Distinct func_call callee names seen in this unit (populated in scan_node).
  absl::flat_hash_set<std::string>              func_call_callees_;
  // Import-const alias per callee module name, used when the natural import name
  // (`const X = import("X.X")`) would EXACTLY collide with a submodule instance
  // variable of the same spelling.  Names are case-sensitive, so the firtool
  // camelCase instance `subModule` does NOT collide with the import const
  // `SubModule`.  Maps module name -> emitted alias (== module name when no
  // collision).  Cleared per module.
  absl::flat_hash_map<std::string, std::string> import_alias_;
  // Callees emitted into the SAME .prp as this unit (call name -> full unit
  // name). They take no import; the call site spells the bare lambda name.
  absl::flat_hash_map<std::string, std::string> same_file_callee_;

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
  absl::flat_hash_map<std::string, std::string>                      folded_attrs_;
  // Per (var,attr) pairs folded above (key "var\x01attr"), so write_attr_set
  // skips re-emitting a folded attr that occurs deeper than the top-level body
  // (e.g. a memory's `mem.[wensize]=N` written inside the always block).
  absl::flat_hash_set<std::string>                                   folded_keys_;
  // Body nets a reg binds as its clock/reset pin (`reg q:[clock_pin=ref <net>]`).
  // A derived clock (`gclk = clk_b & gate`) is an internal combinational signal:
  // the declare-first hoist would emit the reg ahead of `<net>`'s driver, so the
  // `ref` would resolve to the net's pre-driver value (the `clock_pin '0'`
  // tolg error). write_module makes each such net POSITION-INDEPENDENT — a
  // `wire` pre-declare when its single driver allows it, otherwise a minted
  // `wire <net>__pinw` alias assigned the net's final value at the region end
  // (the attr rewritten to `ref <net>__pinw`); the names are stripped (post-SSA).
  absl::flat_hash_set<std::string>                                   pin_dep_nets_;
  // EVERY body net read as the ref value of a folded attribute, for ANY key (a
  // superset of pin_dep_nets_, which covers only the `_pin` keys whose driver is
  // relocated ahead of the declares).  A folded attr rides on its variable's
  // declare, and declares are emitted before every body write — so such a net is
  // read early and cannot be declared in place by its store (single_store_).
  absl::flat_hash_set<std::string>                                   folded_attr_refs_;
  // Structured form of ref-valued folded attributes.  Keeping both directions
  // avoids reparsing/scanning every rendered attribute string for each pin net.
  absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>> folded_attr_refs_by_owner_;
  absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>> folded_attr_owners_by_ref_;
  // Every name the EMITTED attr strings reference: pin_dep_nets_ plus any
  // minted `__pinw` aliases.  Dead-signal removal and instance-output inlining
  // must not fold a name an attr string spells out by name.
  absl::flat_hash_set<std::string>                                   pin_cone_;
  // Wires emitted as `const X:T = nil` forward declarations instead: their
  // single write precedes every read, so position independence is not needed
  // and the `const` form additionally states single-bind / def-before-use.
  absl::flat_hash_set<std::string>                                   const_nil_wire_;
  // Collect the body-variable names a defining statement READS (operands after
  // child0), following single-use folded temps into their definitions so a
  // `gclk = clk_b & inv` whose `& ` is an inlined temp still reports `inv`.
  void               collect_driver_reads(Lnast_nid def_node, absl::flat_hash_set<std::string>& out) const;
  // Same fold-following read collection applied to `node` ITSELF (a condition
  // ref, an if arm, …) rather than a defining statement's operand tail.
  void               collect_node_reads(Lnast_nid node, absl::flat_hash_set<std::string>& out) const;
  void               collect_node_read_ids(Lnast_nid node, absl::flat_hash_set<int32_t>& out) const;
  const std::string& cached_strip_prefix(int32_t name_id) const;
  mutable absl::flat_hash_map<int64_t, std::vector<int32_t>> node_read_ids_cache_;
  mutable absl::flat_hash_map<int32_t, std::string>          stripped_name_cache_;

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
    int32_t                      name_id       = 0;
    Lnast_ntype::Lnast_ntype_int def_type      = Lnast_ntype::Lnast_ntype_invalid;
    int                          def_count     = 0;        // assignments to this name
    int                          decl_defs     = 0;        // of those, bare `declare`s (not value defs)
    bool                         decl_typed    = false;    // a declare carrying a `:T` — inlining would drop it
    int                          use_count     = 0;        // reads of this name
    int                          def_index     = -1;       // pre-order index of the (single) def
    int                          use_index     = -1;       // pre-order index of the (single) use
    int                          min_use_index = 1 << 30;  // pre-order index of the FIRST (earliest) use
  };
  std::unordered_map<std::string, Fold_info>         fold_info_;  // by raw name
  absl::flat_hash_map<std::string, std::vector<int>> write_idx_;  // name -> sorted write pre-order indices
  // Integer-key mirrors for the expression-stability hot path. LNAST names are
  // interned, so recursively hashing/copying the same long strings is pure
  // overhead. Pointers into fold_info_ remain valid across unordered_map rehash.
  absl::flat_hash_map<int32_t, Fold_info*>           fold_info_id_;
  absl::flat_hash_map<int32_t, std::vector<int>>     write_idx_id_;
  absl::flat_hash_map<int32_t, std::string_view>     name_by_id_;
  // Sorted write positions that can actually invalidate a moved read: every
  // write of a multi-write name, plus a single write whose name was read first.
  // Ordinary def-before-use SSA-temp writes cannot be a hazard after a caller's
  // definition and are omitted from this global interval index.
  std::vector<int>                                   stability_hazard_idx_;
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
  absl::flat_hash_map<std::string, int>              func_call_end_idx_;
  absl::flat_hash_set<std::string>                   foldable_;      // temp names to inline at their use
  absl::flat_hash_set<int32_t>                       foldable_id_;   // interned mirror for read-analysis hot paths
  absl::flat_hash_set<int64_t>                       folded_node_;   // def-node keys (get_class_index) to skip
  absl::flat_hash_set<std::string>                   dead_signals_;  // stripped names written but never read (dropped)
  // Every READ name plus its dotted ancestor prefixes (`a.b.c` read -> {a, a.b,
  // a.b.c}). Lets the bundle reconstruction skip re-grouping a NEVER-read tuple
  // base into `wire base:(...) = nil` — detuple only re-splits a tuple that is
  // read, so a write-only bundle would leave field stores tolg cannot lower.
  absl::flat_hash_set<std::string>                   read_field_prefixes_;
  // Mark/collect signals that are assigned but never read (and are not ports,
  // regs, the clock/reset cone, or instance temps) so their def statements are
  // dropped. firtool SSA+poison-init emits a dead base per versioned signal.
  void                                               compute_dead_signals(Lnast_nid io_nid, Lnast_nid stmts_nid);

  // ── mux collapse ──────────────────────────────────────────────────────────
  // An if/unique-if whose every arm is a single value-def to the SAME scalar `x`
  // is a mux: `x=D; if c {x=v}` (Verilog `x = c ? v : D`). Collapse it to one
  // conditional-expression assignment `x = if c0 {v0} elif c1 {v1} … else {D}`,
  // matching the RTL's size AND data-flow complexity. An n-way unique-if (Verilog
  // parallel/`unique case` -> hotmux) stays a `unique if` expression so a
  // Pyrope re-read reconstructs a Hotmux rather than a priority Mux.
  struct Mux_arm {
    Lnast_nid cond;  // condition ref/const
    Lnast_nid def;   // the arm's single value-def stmt (render_def_rhs gives the value)
  };
  struct Mux_info {
    std::string          lhs;             // target scalar (stripped)
    bool                 unique = false;  // was a unique-if
    std::vector<Mux_arm> arms;
    Lnast_nid            else_def;           // else value-def: the else arm, or the preceding default store
    bool                 fold_decl = false;  // emit `mut lhs[:T] = if…` (the poison `mut lhs = 0` declare was dropped)
    std::string          decl_type;          // type suffix for the folded declare (may be empty)
  };
  absl::flat_hash_map<int64_t, Mux_info>      mux_info_;  // keyed by if-node class index
  void                                        analyze_muxes(Lnast_nid stmts_nid);
  // ── expression inlining on top of the mux collapse ────────────────────────
  // Two single-use reader temps render inline at their read, and their def
  // statements (and hoists) disappear:
  //  - `_b2i_N` (bool→int branch merge, arms 1/0) → `unsigned(<cond>)`
  //  - `<base>__wN` (reader SSA version) whose single def is a scalar store →
  //    its RHS value (kills the `const x__w1 = v` line; the poison/mux read it)
  absl::flat_hash_map<std::string, Lnast_nid> bool_inline_;   // stripped name → cond nid
  absl::flat_hash_map<std::string, Lnast_nid> value_inline_;  // stripped name → RHS value nid
  void                                        analyze_expr_inlines(Lnast_nid io_nid, Lnast_nid stmts_nid);
  std::string                                 render_mux_expr(const Mux_info& mi);  // `if c0 {v0} elif … else {D}`
  // The single value-def stmt of a stmts block that writes ONLY scalar `out_lhs`
  // (returns its node), or invalid. If `expect` is non-empty the target must equal
  // it; otherwise the target is reported in out_lhs.
  Lnast_nid                                   arm_value_def(Lnast_nid stmts_node, std::string expect, std::string& out_lhs) const;
  absl::flat_hash_map<std::string, std::pair<std::string, std::string>> range_lohi_;  // range-temp name -> "lo","hi"
  std::vector<Lnast_nid>                 get_mask_nodes_;                             // every get_mask, for range-mask resolution
  std::vector<std::pair<Lnast_nid, int>> tuple_get_nodes_;                            // every tuple_get + its pre-order index
  std::vector<std::pair<Lnast_nid, int>> store_nodes_;                                // every store + its pre-order index
  // Module-instance results (`mut inst = Mod(args)`), stripped names: their output
  // ports may print with dot notation `inst.port` (instead of `inst["port"]`).
  absl::flat_hash_set<std::string>       instance_results_;
  // Instance-output extraction temps (`_t = inst["port"]`) selected to be inlined
  // as `inst.port` at every use; their hoisted `wire`/`mut` declaration is dropped.
  absl::flat_hash_set<std::string>       instance_output_inlined_;

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
  // Same question, but through the operands whose OWN definition is inlined at
  // this site: those carry their def's reads along, so they must be stable over
  // the same window. See lnast_prp_writer.cpp for the miscompile this exists to
  // stop; the cone shape and the hazard interval index are both precomputed
  // once per unit by build_stability_index().
  bool        operands_stable_deep(Lnast_nid def_node, int d, int u, uint64_t walk_epoch, int walk_depth) const;
  bool        operands_stable_deep(Lnast_nid def_node, int d, int u) const {
    // A cyclic or >32-deep cone was already outside the writer's cheap-inline
    // contract. Materialize its intermediates instead of walking a huge,
    // context-sensitive DAG for every candidate. This is conservative:
    // declining a fold changes only Pyrope spelling, never the value graph.
    if (!stability_shape_ok(def_node)) {
      return false;
    }
    // Fast ACCEPT: no write that could invalidate a moved read lands in
    // (d, u) at all, so no cone walk can find one either. This is the common
    // case on generated RTL and is what keeps the analysis out of O(N^2).
    const auto next_hazard = std::upper_bound(stability_hazard_idx_.begin(), stability_hazard_idx_.end(), d);
    if (next_hazard == stability_hazard_idx_.end() || *next_hazard >= u) {
      return true;
    }
    // A hazard write is in the window, but the index is unit-global: only the
    // precise cone walk can say whether it touches THIS candidate's operands.
    // Answering "no" here instead would decline folds the emitted Pyrope shape
    // depends on (inou/slang:slang_concat_sext greps for the folded spelling).
    return operands_stable_deep(def_node, d, u, ++stability_walk_epoch_, 0);
  }
  // Two low bits encode unseen/on-stack/done; the remaining bits identify the
  // current top-level query (bumped per query, so stale marks never alias).
  // This replaces two freshly allocated hash sets per candidate with O(1),
  // allocation-free interned-name marks.
  mutable std::vector<uint64_t>         stability_walk_state_;
  mutable uint64_t                      stability_walk_epoch_{0};
  // Memoized shape of the may-inline dependency DAG. The old per-candidate walk
  // rediscovered cycles and >32-deep paths every time; these are independent of
  // the (def,use) stability interval and can be classified once.
  absl::flat_hash_map<int32_t, uint8_t> stability_shape_state_;  // 1=visiting, 2=done
  absl::flat_hash_map<int32_t, uint8_t> stability_shape_depth_;  // capped at 33
  absl::flat_hash_set<int32_t>          stability_shape_cyclic_;
  uint8_t                               summarize_stability_shape(int32_t name_id);
  bool                                  stability_shape_ok(Lnast_nid def_node) const;
  void                                  build_stability_index();
  // Order-independent "this name could be folded into its uses" test; see the
  // definition. Used only by the stability-shape analysis.
  bool                                  may_inline_name_id(int32_t name_id) const;
  // True if `name` is a `___tmp` selected for inlining.
  bool                                  is_foldable(std::string_view name) const { return foldable_.count(std::string(name)) != 0; }
  // True if a node's def-key is in folded_node_ (its statement is suppressed).
  bool is_folded_node(Lnast_nid nid) const { return folded_node_.count(nid.get_class_index().value) != 0; }

  // Render a value node (ref/const, or a foldable temp's inlined expression) to
  // a Pyrope expression string.  operand_ctx => parenthesise a loose (infix /
  // unary) sub-expression so precedence is preserved when it nests inside
  // another operator; tight postfix forms (`x#[..]`, `x[i]`, `x.[a]`) never get
  // wrapped.
  std::string                      render_value(Lnast_nid node, bool operand_ctx);
  // Render the RHS expression (everything after `lhs = `) of a value-producing
  // "defining" node, inlining folded operands recursively.
  std::string                      render_def_rhs(Lnast_nid def_node, bool operand_ctx);
  // The heavyweight render_def_rhs cases, each its own function so the COMMON
  // recursive spine (render_def_rhs -> render_value -> render_def_rhs, one level
  // per folded temp) carries a small frame: at -O0 every local of every case of
  // a single big function is live for the whole call, and the merged 13.8 KiB
  // frame is what overflowed a 512 KiB worker stack on CVA6 (2026-08-21).
  std::string render_infix_rhs(Lnast_nid def, Lnast_ntype::Lnast_ntype_int t, std::string_view sym, bool operand_ctx);
  std::string render_get_mask_rhs(Lnast_nid c0, bool operand_ctx);
  std::string render_concat_rhs(Lnast_nid c0, bool operand_ctx);
  std::string render_tuple_get_rhs(Lnast_nid c0);
  // `(s)` when a loose (infix / unary) spelling sits as an operand of another
  // operator, else `s` unchanged -- parens only where precedence needs them.
  static std::string wrap_operand(std::string s, bool operand_ctx, bool loose);
  // A const leaf -> its Pyrope spelling (number / true|false|nil / quoted string).
  std::string                      const_text(Lnast_nid node) const;
  // The canonical (Dlop::to_pyrope) spelling of a numeric literal's text; any
  // non-numeric / unparseable text passes through unchanged.
  static std::string               canonical_const_text(std::string_view txt);
  // `0sb?` when this store's value is an all-`?` literal exactly as wide as the
  // target's DECLARED width, else "" (print the literal). See the definition.
  std::string                      x_poison_shorthand(Lnast_nid val_nid, std::string_view lhs) const;
  // True when `val_nid` is an all-`?` literal exactly `bits` wide.
  bool                             is_x_poison_of_width(Lnast_nid val_nid, int bits) const;
  // Width of an all-`?` literal (0 when it is not one, or is a single bit).
  int                              x_poison_width(Lnast_nid val_nid) const;
  // Names whose EMITTED declaration (port signature or `:T` suffix) states the
  // width, so a width-taking `0sb?` re-parses to the same value.
  absl::flat_hash_set<std::string> typed_emitted_;

  // ── Utilities ────────────────────────────────────────────────────────────
  // True for a compiler SSA temp: a raw `%`-prefixed name (or legacy `___`
  // during the migration), OR a name this writer already mapped to an emittable
  // `t…` form (so post-strip_prefix call sites still recognise it as a temp).
  bool                    is_tmp(std::string_view name) const;
  static bool             is_writer_temp_name(std::string_view name);
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
  std::string                                           emit_name_for(std::string_view tmp) const;
  // `<base>___ssa_<N>` -> a FREE `<base>__w<M>` (M >= N). `__wN` is the
  // writer's own output namespace, so a re-emitted body can already hold the
  // natural target; landing on it would merge two variables into one emitted
  // identifier. Memoized so the def and every use agree.
  void                                                  seed_emit_names() const;
  std::string                                           ssa_emit_name_for(std::string_view name, size_t pos) const;
  mutable absl::flat_hash_map<std::string, std::string> ssa_emit_names_;     // <base>___ssa_<N> -> <base>__w<M>
  mutable absl::flat_hash_map<std::string, std::string> tmp_emit_names_;     // %head -> t<id>
  mutable absl::flat_hash_set<std::string>              used_emit_names_;    // every output name taken
  mutable absl::flat_hash_set<std::string>              emitted_tmp_names_;  // mapped t<id> that ARE temps
  mutable bool                                          emit_names_seeded_{false};
};
