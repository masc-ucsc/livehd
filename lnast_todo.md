# LNAST TODO

Consolidated punch-list for the LNAST layer: producers (`inou/prp`,
`inou/slang`, `inou/pyrope`), consumers (`pass/lnast_tolg`, `pass/lnastfmt`,
`pass/lnastopt`, `upass/*`), and alignment with the new Pyrope defined under
`../tree-sitter-pyrope` and `../docs/docs/pyrope`.

Generated from a survey of: `lnast/lnast_nodes.def`, `lnast/lnast_create.cpp`,
`lnast/lnast.cpp`, `lnast/lnast_writer.cpp`, `lnast/tests/ln/*.ln`,
`inou/prp/prp2lnast.cpp`, `inou/pyrope/prp_lnast.cpp`,
`inou/slang/slang_tree.cpp`, `pass/lnast_tolg/*`, `pass/lnastfmt/*`,
`pass/lnastopt/*`, `upass/*`, `docs/docs/pyrope/12-lnast.md`,
`docs/docs/pyrope/14a-small_pyrope.md`, `tree-sitter-pyrope/grammar.js`.

---

## Status summary

### A. Done

The following items were completed in the 2026-04-19/20 session. Each bullet
is verified with a file:line pointer so the next pass can confirm.

- **§1.1 Tmp-var prefix unified to `___<n>`** across all three producers.
  `lnast/lnast.cpp:988` (was `___T`), `inou/prp/prp2lnast.cpp:1288` (was
  `___t`), `inou/pyrope/prp_lnast.cpp:29` (was `___t`),
  `lnast/lnast_create.cpp:12` already `___<n>`.
  The `size() > 4` length guard in `lnast/lnast.cpp:806` adjusted to
  `size() > 3` so `___0..___9` are still classified as tmp.
- **§1.2 / §6.1 Undotted I/O in `inou/prp`.** `inou/prp/prp2lnast.cpp:1227-1229`
  now emits `$foo` / `%foo`, matching every downstream consumer.
- **§15.2 `delay_assign` LNAST node.** New node in
  `lnast/lnast_nodes.def` with `(dst tmp, src ref, offset const|ref)` shape.
  Emitted by `Lnast_create::get_lnast_name(..., last_value=true)` at
  `lnast/lnast_create.cpp:28-34` instead of `attr_get X __last_value`.
  Dispatched by `pass/lnast_tolg/lnast_tolg.cpp` (new
  `process_ast_delay_assign_op`); offset=1 lowers to the same wire-placeholder
  path as the old `__last_value`. Other offsets error out. `pass_lnastfmt`
  duplicates it, `lnast_writer.cpp` renders it as `tmp = delay_assign(src,
  offset)`, `upass_core.hpp` exposes a virtual hook.
- **§7.3 / Tier 4 partial: `PROCESS_NODE` coverage.** Added `attr_set`,
  `attr_get`, `func_def`, `tuple_concat`, `for`, `while`, `uif`, `range`,
  `cassert`, `type_def`, `type_spec`, `delay_assign` virtual hooks in
  `upass/core/upass_core.hpp:27-95`.
- **§7.3 `upass_constprop` `%out` hack generalized.**
  `upass/constprop/upass_constprop.cpp:15-25` strips any leading `%` or `$`
  from the LHS, not just the literal `"%out"`.
- **Fixed: `inou/code_gen/lnast_map.hpp` operator-name misalignment.** Adding
  `delay_assign` to `lnast_nodes.def` shifted the enum index of every node
  after `mut`, so the three hand-maintained `namemap_{pyrope,verilog,cpp}`
  arrays in `lnast_map.hpp` printed the wrong operator string for `bit_and`
  onwards (e.g. `shl` rendered as `">>"`, `bit_or` as `"~"`). This made the
  `inou.code_gen.prp` roundtrip produce malformed pyrope, which then
  dead-looped or crashed when re-fed into the compiler. Inserted
  `"delay_assign"` at position 13 of all three arrays. Root cause:
  `lnast_map.hpp` is hand-maintained while `Lnast_ntype::namemap` is
  auto-generated from the `.def` file — see F.11 for the general-fix note.
- **§15.1 `__create_flop` → `attr_set X storage reg`** landed 2026-04-20.
  Producer `inou/pyrope/prp_lnast.cpp:1631` now emits an `attr_set` node
  `(ref X, const "storage", const "reg")` in place of the old
  `attr_get ___t X __create_flop`; consumer `pass/lnast_tolg/lnast_tolg.cpp`
  dispatches on `storage = reg` inside `process_ast_attr_set_op` to create
  the `Ntype_op::Flop` node (the old `__create_flop` branch in
  `process_ast_attr_get_op` is gone). `pass/lnast_fromlg/pass_lnast_fromlg.cpp`
  emits the new form when going LG→LNAST. `inou/code_gen/code_gen.cpp`
  `do_attr_set` renders the `storage = reg` case back as
  `X = X.__create_flop` so the prp2prp round-trip re-parses cleanly; other
  attr_set fields render as `X.<field> = <value>`. All 48 `pass/compiler`
  tests green; 336/336 global `bazel test` green.
- **§15.2 `__last_value` → `delay_assign` in `inou/pyrope`** landed
  2026-04-20.  `inou/pyrope/prp_lnast.cpp` now emits
  `delay_assign(tmp, src, 1)` instead of `attr_get(tmp, src, __last_value)`
  (shape matches `Lnast_create::get_lnast_name(..., last_value=true)`). The
  `__last_value` branch in `pass/lnast_tolg/lnast_tolg.cpp
  process_ast_attr_get_op` is gone — deferred reads flow through the
  existing `process_ast_delay_assign_op`. `inou/code_gen/code_gen.cpp`
  grew a `do_delay_assign` that renders offset=1 back as
  `dst = src.__last_value` (or inlines the expression via `ref_map` when
  `dst` is a tmp) so the prp2prp round-trip stays symmetric. Multi-field
  paths like `foo.a.__last_value` collapse to `delay_assign(tmp, foo, 1)` —
  a known lossy approximation that also existed under the old `attr_get`
  lowering (`driver_vname = c1_aget`), kept as-is here to avoid scope
  creep; proper path-aware deferred reads need §15.2 offset lowering
  (currently `process_ast_delay_assign_op` only produces an `Or`
  placeholder).
- **§5 partial: lnastfmt rejects `__create_flop` / `__last_value`** landed
  2026-04-20. `Pass_lnastfmt::validate_legacy_magic_strings` walks the LNAST
  before the format phase and errors with a §15 pointer if any const node
  named `__create_flop` or `__last_value` appears. Pins the §15 migration —
  any producer that regresses will fail the formatter loudly rather than
  silently re-introducing the magic strings.
- **F.11 partial: enum-size `static_assert` in `lnast_map.hpp`** landed
  2026-04-20. Added `static_assert(namemap_*.size() == Lnast_ntype::Lnast_ntype_last_invalid)`
  for all three namemaps (pyrope, verilog, cpp). The check fired on the
  existing code, exposing 17 missing type-node entries (`type_def`,
  `type_spec`, `none_type`, `prim_type_*`, `comp_type_*`, `expr_type`,
  `unknown_type`) that had never been added after those enum values landed.
  All three arrays now have 74 entries matching `Lnast_ntype_last_invalid`;
  future node adds will fail compilation until the arrays are updated.
  Full X-macro generation from `lnast_nodes.def` still pending.
- **§D dead code cleanup** landed 2026-04-20. Deleted the following dead
  code paths from `lnast/lnast.cpp` + `lnast/lnast.hpp`:
  `is_scalar_attribute_related` (always-false `#if 0` body); its only callers
  in `trans_tuple_opr` and `trans_tuple_opr_if_subtree` (both `else if`
  branches); `selc2attr_set_get` (dead once its only callers were removed);
  `merge_hierarchical_attr_set` (`#if 0` no-op body called only from the
  removed `selc2attr_set_get`); `collect_hier_tuple_nids` (called only from
  the removed `merge_hierarchical_attr_set`); `rename_to_real_tuple_name`
  (entirely `#if 0`, callers were in `#if 0` blocks);
  `trans_tuple_opr_handle_a_statement` (same); and
  `check_tuple_var_1st_scope_ssa_table_parents_chain` (entirely `#if 0`,
  only referenced in comments). Net: 313 lines removed from `lnast.cpp`.
  Also cleaned up the `_._<n>` legacy tmp-prefix branches in
  `Pass_lnastfmt::is_temp_var` and `Code_gen::is_temp_var` (dead since
  §1.1 — no producer emits that prefix anymore).
- **§D follow-up dead-code cleanup** landed 2026-04-20. Subsequent pass
  through `lnast/lnast.cpp` + `lnast/lnast.hpp` found and removed more
  dead code that survived the earlier deletion: `sel2local_tuple_chain`
  (177-line function, declared in hpp but never called — its only callers
  were in the `#if 0` branches removed in the first §D pass);
  `is_special_case_of_sel_rhs` (defined + declared, never called);
  `ssa_rhs_handle_a_operand_special` (defined + declared, never called);
  `update_tuple_var_1st_scope_ssa_table` (defined + declared, only
  referenced from comments inside the now-deleted `sel2local_tuple_chain`);
  orphan declarations `check_tuple_table_parents_chain` and
  `update_tuple_var_table` (no definitions existed); `is_lhs` (entire body
  `#if 0`, always returned `false`; remaining callers were in the dead
  functions above). Dead state: `tuple_var_1st_scope_ssa_tables` member +
  writes + `Tuple_var_1st_scope_ssa_table` type alias (write-only after
  §D); `tup_internal_cnt` and `collected_hier_tuple_reg_name` (unread
  members). Also simplified `Selc_lrhs_table` from
  `absl::flat_hash_map<Lnast_nid, std::pair<bool, Lnast_nid>>` to
  `absl::flat_hash_map<Lnast_nid, Lnast_nid>` — the `.first` bool was only
  read by the deleted `is_lhs` — and collapsed the two-branch write loop
  in `analyze_selc_lrhs_handle_a_statement` accordingly. The remaining
  `#if 0` block in that function (hier-tuple case, referenced the dead
  `.first` API) also went. Net: additional 276 lines from `lnast.cpp`,
  15 from `lnast.hpp`; 9/9 lnast tests, 9/9 lnastfmt/code_gen tests,
  48/48 pass/compiler tests green.
- **Fixed: `Bitwidth::process_attr_set_dp_assign` iterator-invalidation UAF**
  at `pass/bitwidth/bitwidth.cpp`. Previous code held an iterator `it` from
  `bwmap.find(...)`, then called `bwmap.insert_or_assign(..., it->second)`
  inside a loop. The insert can rehash `bwmap`, invalidating `it`; the next
  iteration's `it->second` is a use-after-free. Caught by AddressSanitizer on
  `pyrope_compile-connect_through2`. Fix: copy `it->second` into a local
  `Bitwidth_range` before the loop. Same pattern also fixed at
  `bitwidth.cpp:1532` (lambda inside `bw_pass`).
- **Fixed: `Cgen_verilog` static init race** at `inou/cgen/cgen_verilog.cpp`.
  `Inou_cgen::to_cgen_verilog` dispatches one `Cgen_verilog p(...)` per
  Lgraph on the thread pool. Each constructor ran
  `if (reserved_keyword.empty()) { lock; insert(...); }` — the `empty()`
  check was outside the lock, so a second thread could observe the set
  mid-insert and start calling `contains()` on it. On `-c dbg` that tripped
  absl's `AssertNotDebugCapacity` kReentrance check, intermittently crashing
  `pyrope_compile-connect_through2` (2/10 flaky). Replaced the TOCTOU pattern
  with `std::call_once`. Fully reliable now (15/15 green).
- **F.7 partial: prefix-char helper delegation** landed 2026-04-20.
  `lnast/lnast_create.cpp:24` replaced direct `lname[0] == '$'` with
  `Lnast::is_input(lname)`. `pass/lnast_tolg/lnast_tolg.hpp:86-88`
  `is_register` / `is_input` / `is_output` now delegate to the
  canonical `Lnast::` helpers instead of duplicating the `front() ==`
  test. The remaining direct `front()` / `[0] ==` prefix checks
  (~15 sites across cgen, semantic, code_gen, bundle, lgtuple,
  upass, fromlg, yosys) stay — they vanish under §12 when
  `$`/`%`/`#` prefixes move to ST attributes. Keeping those for now
  avoids a redundant migration.
- **§7.2 opaque `delay_assign` in lnastopt** landed 2026-04-20.
  `pass/lnastopt/opt_lnast.cpp process_stmts` now has an explicit
  no-op case for `Lnast_ntype_delay_assign`. Previously it fell
  through `default: process_todo` and printed "not handling lnast
  type:delay_assign (TODO)" every time. Matches §15.2 intent — the
  node is opaque to copy-prop because non-zero offsets are never a
  pure copy. No functional change; just quieter output.
- **F.1 dead `Lnast::create_tmp_var` + `tmp_var_cnt` removed** 2026-04-20.
  Neither had any caller (verified via `grep create_tmp_var` — only
  the definition and declaration matched; `tmp_var_cnt` was only
  read by `create_tmp_var` itself). Deleted the method and the
  counter from `lnast/lnast.{hpp,cpp}`. The "two independent tmp-var
  counters" concern in §F.1 is dissolved — only
  `Lnast_create::tmp_var_cnt` remains in use.
- **§13 partial: canonical `Lnast::is_tmp` helper** landed 2026-04-20.
  `lnast/lnast.hpp:148` adds `static bool Lnast::is_tmp(std::string_view)`
  next to `is_register` / `is_input` / `is_output`. Three duplicated
  impls now delegate: `Lnast_tolg::is_tmp_var`
  (`pass/lnast_tolg/lnast_tolg.hpp:85`), `Pass_lnastfmt::is_temp_var`
  (`pass/lnastfmt/pass_lnastfmt.cpp:225`), and `Code_gen::is_temp_var`
  (`inou/code_gen/code_gen.cpp:1260`). Inline `substr(0,3) == "___"`
  guards in `lnast/lnast.cpp` (4 sites) and `pass/lnast_fromlg/pass_lnast_fromlg.cpp`
  also migrated to the helper. `pass/semantic/semantic_check.cpp:16`
  `is_temp_var` left alone — its definition is `front() == '_'`
  (single underscore), a semantically distinct check used by the
  semantic checker's own conventions, not the `___<n>` producer
  tmp-prefix. Narrows the string-matching surface §13 full still has
  to rewrite: consumers now call one predicate, which can flip to a
  `Tree_index`-based check once `Lnast_node` carries a tmp bit.
- **§6.3 `inou.slang` registered as an alias for `inou.verilog`** landed
  2026-04-20. `inou/slang/inou_slang.cpp:30-36` adds a second
  `Eprp_method("inou.slang", ...)` pointing at the same
  `Inou_slang::work` with the same required/optional labels. Verified:
  `lgshell -q` → `help inou.slang` prints the alias description.
  Any future doc that types `inou.slang` now resolves. The `adder.v`
  half of the original §6.3 item was a red herring (no real doc
  referenced it — only the TODO self-referenced).
- **§7.4 `pass/upass` vs `upass/runner` not actually duplicated** —
  documented 2026-04-20. The two directories are layered, not
  redundant: `pass/upass` (~180 LOC) is the EPRP plugin shell that
  registers `pass.upass` and dispatches by IR mode; `upass/runner`
  (~900 LOC) contains `uPass_runner` / `uPass_runner_lgraph` core
  implementations. The original "duplicated setup code" note was a
  misreading. No code change; §7.4 text updated to reflect the
  architecture.
- **§4.3 SSA subscript separator unified** landed 2026-04-20.
  `Lnast::dump` at `lnast/lnast.cpp:639-650` previously rendered SSA refs as
  `name___subs` — using the same `___` separator that also marks tmp
  variables. `Lnast::get_sname` (`lnast/lnast.hpp:154-160`) had long rendered
  the same field as `name|subs` and skipped it when `subs == 0`. The two
  renderings disagreed, and the `___` variant produced a collision-prone
  text form that conflated real SSA numbering with tmp-var-prefix
  matching. Dump now prints `name|subs` only when `subs != 0`, matching
  `get_sname`. Purely cosmetic — no consumer parses dump output — but
  removes the last `___`-as-ssa-separator site. Tests: 336/336 green.
- **§6.1 prp unary-minus canonicalized to 3-child form** landed 2026-04-20.
  `inou/prp/prp2lnast.cpp process_unary_expression` emits
  `minus(dst, const "0", arg)` for unary `-`, matching the binary-minus
  shape that every downstream consumer already expects. Fixes a latent
  correctness bug: under the old 2-child form, `Lnast_tolg::nary_node_rhs_connections`
  wired the single operand to Sum's `A` pin (the `is_first` branch hides
  the `is_subt` path on the first operand), so unary `-x` evaluated as
  `+x`. Full `bazel test //...` green (336/336) — the test corpus
  hadn't been exercising unary-minus on expressions the fold later
  checked, so the bug went unnoticed. Other unary ops (`!`, `not`, `~`)
  remain 2-child because they are genuinely unary.

### B. Build-hygiene fixes (prerequisites)

`bazel clean --expunge` on this toolchain surfaced latent `-Werror` issues in
the baseline. Fixed inline because they blocked `inou/prp` / `lnast` rebuilds:

- `core/mpmc.hpp:55` — `bzero` on non-trivially-copyable type → cast to
  `void*`.
- `lnast/symbol_table.cpp:112-123` — `string_view` dangling on `absl::StrCat`
  temporary → store as `std::string`.
- `lnast/bundle.cpp:708-727,1043-1076` — same dangling-view patterns fixed
  two sites.

### C. Pending — not touched this session

Keep as originally scoped. Each still has a file:line pointer in the
detailed sections below.

| Tier | Item                                                                    | Why deferred |
|------|-------------------------------------------------------------------------|--------------|
| 1    | §5 / §8.1 Golden-output harness                                         | Unblocked now that slang/lgshell build. Snapshot three-producer baselines so future migrations can diff against them. |
| 1    | §2 Doc ↔ `lnast_nodes.def` name drift                                    | Docs live in `../tree-sitter-pyrope/*_syntax_doc/`, outside this repo. |
| 2    | §13 full — `Tree_index`-identity tmps (no string)                       | Partial done 2026-04-20 (see §A): `Lnast::is_tmp` is the single canonical predicate, the three wrapper impls delegate to it, and inline `substr(0,3)=="___"` sites in `lnast.cpp` / `pass_lnast_fromlg.cpp` migrated. Remaining: a `Tree_index`-keyed tmp API on `Lnast_node` so consumers don't need any name at all. |
| —    | ~~§15.1 `__create_flop` → `storage = reg` sticky attr~~ DONE 2026-04-20  | See §A. Producer (pyrope), consumer (lnast_tolg), fromlg emitter, and code_gen round-trip all migrated. Slang already went through `Lnast_create` so no change there. |
| 2    | §15.2 ext — offset=0 (reg Q) / offset=-1 (past cycle) / ref offsets     | `process_ast_delay_assign_op` currently only lowers offset=1. Other offsets error out rather than doing useful work. |
| 3    | §10 Symbol-table API + value-attr inference                              | Multi-day effort; needs golden harness as safety net. |
| 3    | §11 Unify `attr_set` / `tuple_set` shape; collapse `assign`             | Touches every producer and every consumer. Gated on §10. |
| 3    | §12 `$` / `%` / `#` → ST-backed direction/storage                        | Gated on §10 and §13 full. |
| 4    | §4.1 / §4.5 `__ubits` / `__sbits` / other `__` attrs → bare names        | Blocked by §10/§11 (need ST to store non-stringly attrs). |
| —    | ~~§4.3 Move SSA subscript out of ref text~~ DONE 2026-04-20              | `Lnast::dump` at `lnast/lnast.cpp:639-650` now renders the SSA subscript as `name|subs` (matching `get_sname`) and only when `subs != 0`. The `___`-as-separator collision with the tmp-var prefix is gone; the dump no longer renders a bogus `name___0` for fresh refs. |
| —    | ~~§6.1 prp unary-`minus` 2-child canonicalization~~ DONE 2026-04-20      | `inou/prp/prp2lnast.cpp process_unary_expression` now emits 3-child `minus(dst, const 0, arg)` for unary `-`. Matches binary minus shape, so no consumer change needed; also fixes a latent correctness bug where `Lnast_tolg::nary_node_rhs_connections` wired the single operand to Sum's A pin, leaving unary `-x` evaluating to `+x`. |
| —    | ~~§6.3 slang command alias, `adder.v` docs~~ DONE 2026-04-20             | `inou/slang/inou_slang.cpp:30-36` now registers `inou.slang` as an alias `Eprp_method` with identical labels, same `Inou_slang::work`. Verified via `lgshell -q`: `help inou.slang` prints the alias description. No real doc referenced `adder.v` — only the TODO self-referenced it; nothing to fix there. |
| —    | ~~F.11 Auto-generate `inou/code_gen/lnast_map.hpp` from `lnast_nodes.def`~~ PARTIAL DONE 2026-04-20 | `static_assert(namemap_*.size() == Lnast_ntype::Lnast_ntype_last_invalid)` added in `lnast_map.hpp`; 17 missing type-node entries filled in. Full X-macro generation still open. |
| —    | ~~§7.4 Merge / document `pass/upass` vs `upass/runner`~~ RESOLVED 2026-04-20 | Not actually duplicated. `pass/upass` (~180 LOC) is the EPRP plugin shell that registers the `pass.upass` command and dispatches to the runner; `upass/runner` (~900 LOC) is the core implementation (`uPass_runner`, `uPass_runner_lgraph`). §7.4 entry updated to reflect the layering; no code change needed. |

### D. Legacy dead-code candidates observed while editing

Resolved 2026-04-20 (this session):

- ~~`lnast/lnast.cpp:134-151` `is_scalar_attribute_related` body `#if 0`~~ DELETED
  along with its callers (both gated branches returned false). The dependent
  chain (`selc2attr_set_get` → `merge_hierarchical_attr_set` → `collect_hier_tuple_nids`)
  was also dead once the callers were removed, so all three were deleted too.
- ~~`lnast/lnast.cpp:294-353` `rename_to_real_tuple_name` entirely `#if 0`~~ DELETED.
  Callers were inside `#if 0` blocks in `trans_tuple_opr` / `trans_tuple_opr_if_subtree`;
  those blocks gone.
- ~~`lnast/lnast.cpp:355-363` `trans_tuple_opr_handle_a_statement` `#if 0` body~~ DELETED
  along with its dead `#if 0` callers.
- ~~`lnast/lnast.cpp:583-611` `check_tuple_var_1st_scope_ssa_table_parents_chain`~~
  DELETED; only callers were in comments (which were also cleaned up).
- ~~`_._<n>` legacy tmp-prefix branches in `pass/lnastfmt/pass_lnastfmt.cpp` and
  `inou/code_gen/code_gen.cpp:1260` `is_temp_var`~~ DELETED. No producer has
  emitted `_._` since §1.1; input-side acceptance was dead.

Resolved 2026-04-20 follow-up pass (same session):

- ~~`sel2local_tuple_chain` (177 lines, cpp:122-298)~~ DELETED — function
  declared in `lnast.hpp` but never called; its only call sites were in the
  `#if 0` branches deleted in the first §D pass.
- ~~`is_special_case_of_sel_rhs` / `ssa_rhs_handle_a_operand_special`~~ DELETED
  — defined and declared, never called.
- ~~`update_tuple_var_1st_scope_ssa_table`~~ DELETED — only referenced from
  comments inside the now-deleted `sel2local_tuple_chain`.
- ~~orphan declarations `check_tuple_table_parents_chain`,
  `update_tuple_var_table`~~ DELETED — never had definitions.
- ~~`is_lhs` (body `#if 0`, always `return false;`)~~ DELETED — all live
  callers were in the deleted functions above.
- ~~`tuple_var_1st_scope_ssa_tables` + `Tuple_var_1st_scope_ssa_table` alias~~
  DELETED — member was write-only after the first §D pass.
- ~~`tup_internal_cnt`, `collected_hier_tuple_reg_name` members~~ DELETED —
  not read anywhere (the first was flagged by `-Wunused-private-field`
  after the other dead functions went).
- ~~Remaining `#if 0` block in `analyze_selc_lrhs_handle_a_statement`
  (cpp:330-353 hier-tuple case)~~ DELETED — referenced the dead
  `Selc_lrhs_table.first` bool.
- ~~`Selc_lrhs_table` `std::pair<bool, Lnast_nid>` → `Lnast_nid`~~ SIMPLIFIED —
  the `.first` bool was only read by the deleted `is_lhs`. The two-branch
  write loop in `analyze_selc_lrhs_handle_a_statement` collapses to a
  single unconditional write.

Remaining: none in this category. The `__last_value` / `__create_flop`
string tests that used to sit inside `#if 0` blocks are all gone along
with their host functions.

### E. Environment / build blockers — RESOLVED 2026-04-20

- **`@fmt//:fmt` now builds.** Bumped from 10.1.1 → 11.1.4 (sha256 pinned) in
  `MODULE.bazel:51-60`. 11.1.4 fixed the clang-21 consteval strictness issue
  that tripped `format-inl.h`. Note: LiveHD source code never used `fmt::`
  directly (the `Pass_lnastfmt` hits on a `fmt::` grep were false positives);
  fmt flows in only as a transitive CMake dep of slang.
- **`@googletest//:gtest_main` now builds.** googletest 1.17.0 is already
  the latest BCR release. Its `gtest-printers.h:528` still triggers
  `-Wcharacter-conversion` (hard error under clang 21 default). Added
  `--copt=-Wno-error=character-conversion` and the `--cxxopt` twin in
  `.bazelrc:9-14` — downgrades just that one diagnostic to a warning.
  Keep until googletest ships a fix upstream, then drop.
- **Smoke: `bazel test -c dbg //...`** now builds all 415 targets, **336 of
  341** test suites pass after the `lnast_map.hpp` fix. `//pass/compiler:all`
  is fully green (48/48). The 5 remaining failures are all yosys-path
  assertions (`yosys_compile-{hier_test,latch,nlatch,noloop,paramods}`) in
  `inou/cgen/cgen_verilog.cpp:89,667` (`!txt_op.empty()`,
  `expr_it != pin2expr.end()`) and `inou/yosys/lgyosys_tolg.cpp:1270`
  (`dpin.get_bits() == lhs_wire->width`). These live in the
  Verilog→Yosys→LGraph lowering path, not touched by any LNAST change in
  this or the prior LNAST-unification commit. Pre-existing, out of scope
  for the LNAST track.

### F. New-iteration LNAST-syntax issues to revisit

Things I noticed while editing that aren't really captured by the existing
§1-§15 punch-list:

1. ~~**Two independent tmp-var counters.**~~ **Resolved 2026-04-20**:
   `Lnast::tmp_var_cnt` and `Lnast::create_tmp_var()` were dead (no
   callers). Both deleted from `lnast/lnast.{hpp,cpp}`. Only
   `Lnast_create::tmp_var_cnt` remains in use (`lnast/lnast_create.cpp`
   emits `___<n>` via `create_lnast_tmp`), so the collision risk
   described here is gone.

2. ~~**`delay_assign` is single-producer today.**~~ **Resolved 2026-04-20**:
   all three producers now emit `delay_assign`. `inou/pyrope/prp_lnast.cpp`
   (around line 1644) emits `Lnast_node::create_delay_assign()` in the
   `__last_value` path; `inou/slang` goes through `Lnast_create`;
   `inou/prp` does not have a `__last_value` emitter yet (see §3.11 —
   surface-syntax producer for `@[N]` / `::[defer]` is still pending in
   `inou/prp`, but that's an emit-site gap, not a dual-format problem).

3. **`delay_assign` offset semantics are partially undefined.** Current
   lowering at `pass/lnast_tolg/lnast_tolg.cpp process_ast_delay_assign_op`
   only handles offset=1 on a wire (identity/placeholder). The full §15.2
   spec needs: offset=0 on a reg (Q pin), offset=-1 on a reg (past cycle),
   comptime-ref offsets (e.g., `xx = 3; a@[xx]`), and validation that
   non-1 offsets on wires / 0-offset on wires are rejected at lnastfmt.

4. ~~**Unary `minus` is a 2-child `minus` node.**~~ **Resolved 2026-04-20**
   (§6.1). `inou/prp/prp2lnast.cpp` canonicalizes unary `-x` to the
   3-child shape `(dst, const 0, arg)`, matching binary subtraction. No
   new `neg` node was needed; all consumers already handle the 3-child
   form. See §6.1 for details and the latent Sum-pin-wiring bug this
   also fixed.

5. ~~**SSA subscript is rendered two ways.**~~ **Resolved 2026-04-20**
   (§4.3). `Lnast::dump` now uses the same `name|subs` form as
   `Lnast::get_sname`, and both skip the subscript when `subs == 0`.

6. **`assign` SSA pass injects a synthetic `err_var = 0b?`** at
   `lnast/lnast.cpp:37-42` (after §D follow-up renumbering),
   unconditionally prepending it to every top-level `stmts`. Also used
   as the phi-table sentinel for branches where a var is defined on one
   side only (see `undefined_var_nid` refs). The consumer-side guards
   (e.g., `lnast_tolg.cpp:92-95`) explicitly skip `err_var_undefined`.
   Non-trivial to relocate because of the phi-placeholder role; would
   need to replace the phi-side refs with a dedicated `create_undefined()`
   ref node or similar before the top-level emission can be removed.

7. **`Lnast::is_register` / `is_input` / `is_output` live on `Lnast`,
   not `Lnast_node`.** And they test `front()` on a `string_view`. After
   §12 these should vanish entirely — but as long as they exist, they're
   static `Lnast` methods called with a name, which invites "did the
   caller strip the prefix already?" bugs. ~~Example: `Lnast_create.cpp:24`
   gates on `lname[0] == '$'` directly instead of calling the helper.~~
   **Partial 2026-04-20:** `Lnast_create::get_lnast_name` now calls
   `Lnast::is_input(lname)`, and the `Lnast_tolg::is_register/is_input/
   is_output` duplicates delegate to the canonical `Lnast::is_*` helpers.
   Other direct `front()` / `[0] ==` checks in `pass/lnast_fromlg`,
   `inou/code_gen`, `pass/semantic`, `upass/constprop`, `lgraph/lgtuple`,
   `lnast/bundle` remain — they go away entirely under §12.

8. ~~**`_._` tmp prefix reader**~~ **Resolved 2026-04-20** (§D): the
   acceptance was deleted from `Pass_lnastfmt::is_temp_var` and
   `Code_gen::is_temp_var`. Latent issue flagged separately:
   `pass/lnast_fromlg/pass_lnast_fromlg.cpp:82` still *emits* `_._L<n>`
   prefixes on the LG→LNAST reverse path. Compiler tests don't hit a
   path that feeds `pass_lnast_fromlg` output back into
   `Pass_lnastfmt`/`Code_gen`, so no regression today; if that round-trip
   is ever exercised, those vars will now be misclassified as non-tmps.
   Fix owner: whoever revives `lgtoln_verif_*` tests.

9. **`lnast_nodes.def` has six `FIXME: remove`s** (`uif`, `dp_assign`,
   `mut`, `phi`, `hot_phi`, plus `comp_type_mixin`). The TODO references
   this but doesn't schedule the removal. Under §11 collapse, `mut`
   should be deleted; `dp_assign` is explicitly deferred per §11.3. `uif`
   / `phi` / `hot_phi` need owners.

10. **`attr_set` / `attr_get` are variadic in code** (they accept a path
    plus a terminal key/value), but the grammar/shape isn't validated
    anywhere. §11.5 requires lnastfmt to enforce `(root, p1..pN, key, value)`
    for `attr_set` and `(root, p1..pN, key)` for `attr_get`. No validation
    runs today.

11. **`inou/code_gen/lnast_map.hpp` is hand-maintained and silently
    breakable.** It holds three `namemap_{pyrope,verilog,cpp}` arrays
    indexed by `Lnast_ntype` enum value — but the arrays are literal lists,
    not X-macro'd from `lnast_nodes.def` like `Lnast_ntype::namemap` in
    `lnast/lnast_ntype.hpp:16-19` is. Adding *any* new node to
    `lnast_nodes.def` therefore silently mis-indexes every subsequent
    operator's printed name in code-gen. The `static_assert` at
    `lnast_map.hpp:192-193` only checks the three arrays agree with *each
    other*, not with the enum. This bit us on `delay_assign` (see A). Fix:
    rewrite `lnast_map.hpp` to X-macro from the .def file with per-node
    overrides, or at minimum add a `static_assert(namemap_pyrope.size() ==
    Lnast_ntype::Lnast_ntype_last_invalid)`. Until that lands, **every new
    LNAST node needs a manual update in all three arrays at the same
    enum-position**.

---

## 1. Convention unification across producers

*Status:*
- §1.1 (tmp-var prefix) and §1.2 (undotted I/O in prp) **done** this session.
- §1.3 (bare attrs vs `__`) and §1.4 (lnastfmt validator) **pending** —
  gated on §10 ST API.

The three LNAST producers disagree on textual conventions that are re-parsed by
downstream passes. These need a single canonical format, enforced by
`pass.lnastfmt` (which currently only removes SSA temporaries and does not
validate naming).

| Convention            | `inou/prp` (new tree-sitter)      | `inou/pyrope` (old)            | `inou/slang`                   |
|-----------------------|-----------------------------------|--------------------------------|--------------------------------|
| Tmp var prefix        | `___<n>` ✅                        | `___<n>` ✅                     | `___<n>` ✅                     |
| Input marker          | `$<name>` ✅                       | `$<name>`                      | `$<name>`                      |
| Output marker         | `%<name>` ✅                       | `%<name>`                      | `%<name>`                      |
| Register marker       | `#<name>` (old)                   | `#<name>`                      | —                              |
| Attribute literal     | `size`, `bits` (bare const)       | `__ubits`, `__sbits`, `__bits` | `__ubits`, `__sbits`           |
| `__create_flop` used? | No (uses `reg`/`:[..]`)           | No — emits `attr_set X storage reg` (§15.1, 2026-04-20) | No (explicit ports)            |
| `__last_value`        | No                                | Yes, pervasive                 | Emits `delay_assign` ✅          |
| SSA suffix            | `name___<n>` after `ssa_trans()`  | Same                           | Same                           |

**Action items**

1. ~~Pick one tmp-var prefix~~ → **done**, canonical is `___<n>`.
2. ~~Decide `$foo` vs `$.foo`~~ → **done**, canonical is `$foo`.
3. Decide if attribute identifiers carry `__` (old Pyrope, slang) or not
   (new Pyrope docs / tree-sitter). See §4, §11 — gated on §10.
4. Make `pass.lnastfmt` perform a *validation* sweep in addition to its SSA
   cleanup. Still pending. Currently `Pass_lnastfmt::is_temp_var` only handles
   `___`/`_._` prefixes and `is_ssa` is a crude `substr(0,2) != "__"` test at
   `pass/lnastfmt/pass_lnastfmt.cpp:203-207`.

---

## 2. Node-name drift between docs and implementation

*Status:* **deferred** — the docs referenced in the original TODO live in
`../tree-sitter-pyrope/{old,new}_syntax_doc/`, which is outside LiveHD.
Aligning them belongs in that repo. No code-side action needed.

`docs/pyrope/12-lnast.md` describes nodes by names that don't exist in
`lnast/lnast_nodes.def`. Either the doc needs an update or the code does.

| Doc name (`12-lnast.md`) | Code name (`lnast_nodes.def`) |
|--------------------------|-------------------------------|
| `tup_add`/`tup_set`/`tup_get`/`tup_concat` | `tuple_add`/`tuple_set`/`tuple_get`/`tuple_concat` |
| `not`, `and`, `or`, `xor` | `bit_not`, `bit_and`, `bit_or`, `bit_xor` |
| `lnot`, `land`, `lor` | `log_not`, `log_and`, `log_or` |
| `reduce_or`, `reduce_and`, `reduce_xor` | `red_or`, `red_and`, `red_xor` |
| `let`, `var` (as top-level assignment variants) | Only `assign`, `dp_assign`, `mut` exist |
| `attr_ref_set`, `attr_ref_check` (sub-nodes of `ref`) | Only top-level `attr_set`/`attr_get` |
| `in`, `has`, `does`, `to`, `range` as first-class ops | Only `range` exists; `is` exists but no `in`/`has`/`does` |
| `set`, `check`, `assign` as three-way distinction | `assign` (+ `dp_assign`, `mut` — both `FIXME: remove`) |

The doc examples freely attach attribute sub-nodes to a `ref`, which LNAST's
current tree shape does not support (a `ref` has no children in the current
implementation). This is the single biggest semantic gap.

**Action items** (unchanged from original TODO)

1. Decide whether LNAST stays with flat `attr_set`/`attr_get` statements or
   adopts the doc's nested `attr_ref_set`/`attr_ref_check` attached to `ref`.
2. Rename doc terminology or `lnast_nodes.def` so they match one-to-one.
3. Add missing first-class ops used in docs: `in`, `has`, `does`, `to`,
   `popcount` (`popcount` *is* in the def file but missing from the doc's
   bit-selection lowering). Also `implies`, `and_then`, `or_else` examples in
   `12-lnast.md` assume specific lowerings that have no matching LNAST node.
4. Resolve the `let`/`var`/`assign`/`const`/`mut`/`reg` storage-class question.

---

## 3. New Pyrope syntax with unclear LNAST lowering

*Status:* **out of scope** for the current track. The user explicitly scoped
"don't evolve Pyrope to latest tree-sitter grammar" — §3.1 through §3.14 all
assume the new surface syntax and therefore wait until Pyrope migration is
scheduled.

Kept here unchanged as the design notes for that future effort.

### 3.1 Storage classes (`const`/`mut`/`reg`/`comptime`)

```pyrope
comptime SIZE = 16
comptime mut counter = 0
const my_constant = 42
mut my_wire = 0
reg my_state = 0
```

Questions: does LNAST carry a storage-class annotation on `ref`? Is `reg`
modeled via the `#` prefix (today's convention) or a new node? Does `comptime`
become an attribute (e.g., `::[comptime]`) or a type kind?

### 3.2 Attributes on declarations vs reads

```pyrope
mut data:u32:[max=1000, min=0] = 0      // set at decl
cassert counter::[bits] == 8             // read via ::[ ]
```

Two distinct syntaxes (`:[...]` set, `::[...]` read). Current `inou/prp`
emits the same `attr_get` in both cases. Decide LNAST shape for decl-time sets
(nested under the declaration? a separate `attr_set` statement right after?).

### 3.3 Memory and array declarations

```pyrope
reg ram:[1024]u32:[latency=1, rdport=(0,1), wrport=(2,3)] = 0
mut out1 = ram.port[0][addr3]:[rdport=0]
```

Arrays/memories with port attributes have no example in `12-lnast.md`. LNAST
currently has `comp_type_array` but no way to attach a multi-attribute bundle
to it. Port access `ram.port[0][addr]:[rdport=0]` needs a lowering rule.

### 3.4 Ranges with three operators

```pyrope
1..=5      // inclusive
0..<4      // exclusive
2..+3      // size-based (length = 3)
```

`lnast_nodes.def` has a single `range` node with implicit semantics. The doc
shows `a..<b` lowering to a `sub`+`range` pair but `..+` is not spec'd.

### 3.5 Tuple positional-named keys (`:pos:name`)

Docs mention `x.:3:foo = 2` as equivalent to `x[3] = 2` *and* a label-check
that slot 3 has name `foo` (`12-lnast.md:119-123`). This requires a composite
key format that LNAST consumers don't agree on:
- `bundle.cpp` parses `:<pos>:<name>` format internally.
- `upass_constprop.cpp:271` writes `":{pos}:{name}"`.
- `inou/pyrope/prp_lnast.cpp` and `inou/prp/prp2lnast.cpp` never emit this
  form.

Decide: is `:pos:name` a producer-visible convention or purely an internal
bundle key? If external, document the lowering.

### 3.6 Bit-selection modifiers (`#[..]`, `#sext[..]`, `#|[..]`, `#+[..]`)

```pyrope
const t1 = foo#sext[..=4]
const t5 = foo#+[..=4]    // popcount
```

`12-lnast.md` gives a lowering, but `#+` → `popcount` and `#sext` → `sext`
after `get_mask` is not implemented in any current producer. Confirm the
lowering and add tests.

### 3.7 Match expressions and `unique if`

`grammar.js:231-250` defines `match` with a rich `match_operator` list (`has`,
`in`, `case`, `does`, `is`, plus negations). Current LNAST has `if` but no
`match`. Decide: desugar to nested `if`/`hot_phi` (risky for unique semantics)
or add a `match` node.

### 3.8 `puts`, `test`, `assert` vs `cassert`

`assert` (runtime) and `cassert` (compile-time) both appear in docs. LNAST has
`cassert` only. `puts` (debug print) has no LNAST encoding.

### 3.9 Lambda / function types with timing

```pyrope
comb add(a:u8, b:u8) -> (result:u8) { ... }
pipe[1] counter() -> (reg count:u8) { ... }
flow alu(in1, in2) -> (out) { ... }
```

`comp_type_lambda` exists but doesn't carry `comb`/`pipe`/`flow`/`mod`
classification or the `[N]` pipeline depth. Need to either extend the node or
add an attribute.

### 3.10 `ref` parameters

Small Pyrope has `ref` both as storage (`ref x`) and as argument modifier
(`comb(ref self)`). `grammar.js:394` calls it `ref_identifier`. LNAST's `ref`
*node type* is unrelated to this; every variable reference is a `ref` node.
This name collision will bite when implementing argument passing.

### 3.11 Timed identifier `@[...]`

```pyrope
counter@[] += 1     // deferred
a@[-1]              // previous cycle
:@[N]               // timing type check on LHS
```

The `delay_assign` node added in §15.2 covers the offset semantics. Still
needed: a producer emission for each surface form in `inou/prp` (none of
`@[N]`, `@[-1]`, `@[]`, `::[defer]` produce LNAST today).

### 3.12 Enums / variants / `impl` / `type` statements

`tree-sitter-pyrope/grammar.js:261-393` defines `type_statement`,
`impl_statement`, `enum_definition`, `enum_assignment`. LNAST has
`comp_type_enum` and `type_def` (FIXME: rename to `type_bind`) but no
documented lowering for trait-style `impl` blocks.

### 3.13 For-comprehension and for-unrolling

```pyrope
for i in 0..=7 { memory[i] = init_value }   // compile-time unrolled
x = [e*2 for e in data]                     // comprehension in expression
```

LNAST has `for` but the semantic distinction between runtime-loop (none in
Small Pyrope) and compile-time-unroll needs a marker. Comprehensions inside
expressions (`for_comprehension` in the grammar) have no LNAST example.

### 3.14 `when`/`unless` statement gate

`grammar.js:760-767` allows `stmt when cond` / `stmt unless cond`. This is a
single-statement guarded execution with no LNAST example — probably lowers to
`if` but should be documented.

---

## 4. In-band string meaning — inventory and cleanup

*Status:* **pending** — individual entries below are status-tagged. Most
require §10 ST rework to fully resolve.

The LNAST tree currently relies on string-prefix conventions inside `ref` and
`const` nodes to encode semantics. This forces every consumer to string-match
and collides with any identifier that happens to start with those characters.

### Known conventions in use today

| Convention        | Meaning                            | Example location                                         |
|-------------------|------------------------------------|----------------------------------------------------------|
| `$name`           | Graph input                        | `lnast.hpp:170`, `slang_tree.cpp:83`, `prp_lnast.cpp`    |
| `%name`           | Graph output                       | `lnast.hpp:169`, `slang_tree.cpp:90`                     |
| `#name`           | Register / flop                    | `lnast.hpp:168`                                          |
| ~~`$.name`, `%.name`~~ | removed from prp 2026-04-19   | was `inou/prp/prp2lnast.cpp:1227-1229`                   |
| `___<n>`          | Compiler tmp (canonical)           | `lnast.cpp:988`, `lnast_create.cpp:12`, `prp2lnast.cpp:1288`, `prp_lnast.cpp:29` |
| ~~`_._name`~~ | (removed 2026-04-20 §D) Was non-SSA user var, legacy; reader deleted | was `pass_lnastfmt.cpp`, `code_gen.cpp`. Note: `pass/lnast_fromlg` still *emits* `_._L<n>` — latent. |
| `name|<n>`        | SSA subscript (pipe + n; dump + get_sname, 2026-04-20 §4.3) | `lnast/lnast.cpp:639-650`, `lnast/lnast.hpp:154-160` |
| `__ubits`/`__sbits`/`__bits` | Bitwidth attribute      | `lnast_create.cpp:448-450`, `bundle.cpp:925`             |
| ~~`__create_flop`~~ removed 2026-04-20 | was Register constructor | replaced by `attr_set X storage reg` (§15.1). Residual `#if 0` block now deleted (§D follow-up). |
| ~~`__last_value`~~ removed 2026-04-20 (§15.2) | was pre-update value of reg | replaced by `delay_assign` in all producers; lnastfmt rejects any re-introduction. Residual `#if 0` block now deleted (§D follow-up). |
| `__dp_assign`     | Deferred-parent assign marker      | `lnast.cpp:145`, `cgen_verilog.cpp:1023`                 |
| `__reset`         | Reset pin for a register           | `inou/pyrope/tests/cnt_attr7.prp`                        |
| `__mask`          | Bit-mask-derived name              | `inou/yosys/lgyosys_tolg.cpp:2374`                        |
| `__fir_const`     | FIRRTL constant marker (sub-name)  | `inou/locator/traverse_lg.cpp:176+`                      |
| `__range_begin`/`__range_end` | Range endpoints         | `inou/code_gen/code_gen.cpp:221-263`                     |
| `__valid`/`__retry` | Dataflow handshake attributes    | `inou/code_gen/code_gen.cpp:842-845`                     |
| `0.<attr>` vs `__<attr>` | Root-attribute canonicalization | `lnast/bundle.cpp:736-759`                         |
| `:pos:name`       | Bundle composite key               | `upass_constprop.cpp:271`, `bundle.cpp:745`              |
| `__` but `___` is NOT attribute | Attribute predicate   | `lnast/bundle.cpp:736`, `inou/pyrope/prp_lnast.cpp:1612`, `upass_constprop.cpp:377` |

### What to rethink

1. **Attribute vs identifier conflict.** *Pending.* Proposal: drop the `__`
   prefix and carry attribute-ness via node-kind — e.g., a dedicated
   attribute-key node or the `attr_set` / `attr_get` statement always
   tagging its last child as an attribute name. Gated on §10/§11.

2. **`%`/`$`/`#` I/O marker in `ref` text.** *Pending.* Partial progress:
   the dotted `$.` / `%.` variant was removed from prp. Full removal is
   §12.

3. ~~**SSA subscript collision with tmp vars.**~~ **Resolved 2026-04-20**
   (§4.3). `Lnast::dump` at `lnast/lnast.cpp:639-650` now renders the
   subscript as `name|subs` (matching `get_sname`) and skips it when
   `subs == 0`. The `___` separator no longer appears in dump output, so
   there is no textual collision between SSA subscripts and the `___<n>`
   tmp-var prefix.

4. **Tmp-var prefix proliferation.** *Done for strings* (§1.1 above).
   `Tree_index`-identity tmps (§13) **pending**.

5. **`__last_value` and `__create_flop` as magic constants.**
   Both **done 2026-04-20**. `__create_flop` → `attr_set X storage reg`
   (§15.1), `__last_value` → `delay_assign(tmp, src, 1)` (§15.2). Both
   migrations cover the pyrope producer, the slang path (already
   handled via `Lnast_create`), `pass/lnast_tolg` consumer,
   `pass/lnast_fromlg` LG→LNAST emitter, and the `inou/code_gen` prp2prp
   round-trip. `Pass_lnastfmt` errors if either string reappears as a
   const (§5 partial).

6. **`:pos:name` bundle key.** *Pending.* Decide if it's a language
   feature (document in `12-lnast.md` §Tuples) or a symbol-table
   implementation detail (hide it).

---

## 5. `pass.lnastfmt` extension — a consistency checker

*Status:* **pending.** No validator implemented yet. Golden-output harness
also pending — blocked by the slang build issue.

`pass/lnastfmt/pass_lnastfmt.cpp` today does only one job: remove SSA
subscripts and collapse assign-of-tmp chains. The pass should grow into a
format *validator* run before every LNAST consumer (`lnast_tolg`, `upass`,
`code_gen`). Minimum checks:

1. Every `ref` text matches one of: `$<name>`, `%<name>`, `#<name>`,
   `___<digits>` (tmp), `<alpha_id>`, `<alpha_id>___<digits>` (SSA).
2. Every `const` text is either a numeric literal (parseable by
   `Lconst::from_pyrope`), a `"string"` literal, or an attribute identifier
   from a documented whitelist (or drops the `__` prefix per §4.1).
3. `assign` has exactly 2 children (lhs `ref`, rhs `ref|const`). Today
   `pass_lnastfmt.cpp:197` only asserts this during `observe_lnast`, not as a
   reportable error.
4. `tuple_set`/`tuple_get`/`tuple_add`/`attr_set`/`attr_get` child shapes
   match the docs in `12-lnast.md`. The docs are the spec; lnastfmt should
   enforce them.
5. Unary vs binary `minus` / `bit_not` arity is unambiguous. Right now
   `lnast.cpp` and consumers distinguish unary `-x` (2 children: dst, src)
   from binary `a-b` (3 children: dst, a, b) by *counting children*. An arity
   check is easy; better still, split into `neg` and `minus`.
6. `dp_assign`, `mut`, `uif`, `phi`, `hot_phi` — all marked `FIXME: remove` in
   `lnast_nodes.def`. The formatter should warn when a fresh producer emits
   them and reject them in CI once the migration is done.
7. `delay_assign` — validate shape (child 0 tmp, child 1 declared ref,
   child 2 comptime const); reject offset=0 on non-reg src.

This also gives us a tool to compare the three producers on the same input
(once a common source language exists — probably the new small-Pyrope) and
diff-check their LNAST output.

---

## 6. Producer-specific issues found during this survey

### 6.1 `inou/prp` (new tree-sitter-based)

- ~~Dotted I/O (`$.name`, `%.name`)~~ — **done**, now emits `$name`/`%name`.
- ~~**Unary minus lowering**~~ — **done 2026-04-20.**
  `inou/prp/prp2lnast.cpp process_unary_expression` (line ~1030) now emits
  the 3-child canonical form `minus(dst, const "0", arg)` when the operator
  is `-`, matching the shape of binary `a-b`. Consumers (`Lnast_tolg`,
  `pass_lnastfmt`, `pass_lnast_fromlg`, the `upass` tree) already handle
  3-child `minus` correctly, so no consumer change was needed. This also
  fixes a latent correctness bug: with the old 2-child form, `Lnast_tolg::
  nary_node_rhs_connections` wired the single operand to Sum's `A` pin
  (because `is_first=true` suppressed the `is_subt` branch), so unary
  `-x` came out as `+x`. Other unary ops (`!`, `not`, `~`) stay 2-child
  because they are genuinely unary. Note: `inou/slang` already emitted the
  canonical form via `create_minus_stmts("0", lhs)`. Dedicated `neg` node
  not introduced — the 3-child form is the least-invasive unification.
- Attribute access `a.[size]` correctly lowers to `attr_get` with bare
  `const size` — this is the *future* convention per §4.1. The old
  producers still emit `__size`/`__ubits`/etc. The inconsistency means
  `upass_constprop.cpp:375-381` (which strips `__*` attributes) silently
  works on old output and silently *fails to strip* new output.
- `fdef not supported yet` warning still fires on
  `inou/prp/tests/comptime/attr_size.prp` (lambda inside expression).
  Needed for most real tests.
- `inou/prp/tests/comptime/*.prp` are parse-smoke tests only; no assertions
  on the generated LNAST. Adding golden-output comparison would catch the
  §1 drift automatically.

### 6.2 `inou/pyrope` (old PRP parser)

*Status:* frozen per user direction ("don't evolve Pyrope").

- Uses an older Pyrope grammar that does not match `tree-sitter-pyrope`. The
  three TODO items in `inou/pyrope/TODO.md` (fcall args, missing
  `..fname..` method call, redundant `assign` after expression) are still
  open; the new parser should not re-implement these bugs.
- Plan: freeze `inou/pyrope` feature work, migrate tests to `inou/prp` as
  that stabilizes, delete `inou/pyrope` when covered.
- ~~Still emits `__create_flop` and `__last_value`~~ migrated
  2026-04-20 to `attr_set storage reg` (§15.1) and `delay_assign`
  (§15.2). No producer in the tree emits either string anymore.

### 6.3 `inou/slang`

- ~~Tmp-var prefix `___<n>`~~ — **already canonical**; no change needed.
- Now emits `delay_assign` (via `Lnast_create::get_lnast_name`) instead of
  `attr_get __last_value`.
- ~~`adder.v` test file referenced in docs is still missing; `inou.slang`
  isn't a registered command either~~ — **done 2026-04-20.**
  `inou/slang/inou_slang.cpp Inou_slang::setup` now registers
  `inou.slang` as a second `Eprp_method` pointing at the same
  `Inou_slang::work`, with the same label set as `inou.verilog`.
  `lgshell -q` confirms `help inou.slang` works. The `adder.v` half
  of the item was a red herring: the only references to `adder.v` in
  this repo were in this TODO and a working note (`pp.md`); no real
  docs depended on it.
- `process_top_instance` at `slang_tree.cpp:113` — the `I(false)` is
  actually unreachable in practice (InterfacePort is handled in its own
  branch with a FIXME log). The TODO's description was slightly wrong;
  first SV interface test will print FIXME and silently skip, not crash.
- Slang targets don't build on this toolchain until §E's fmt issue is
  resolved.

---

## 7. Consumer-side assumptions to document or relax

### 7.1 `pass/lnast_tolg`

*Status:* pending (§12).

- `lnast_tolg.cpp:40-42` assumes the graph always has `$` and `%` top-level
  IOs. If a module has no outputs, `get_graph_output("%")` still has to be
  valid. Confirm this still holds under the §4.2 cleanup.
- Many `FIXME` comments embed the `__xxx` string assumption — once §4.1
  lands, these need auditing.
- `process_ast_delay_assign_op` (new) dispatches offset=1; other offsets
  error out. Needs full §15.2 offset support when the time comes.

### 7.2 `pass/lnastopt`

- Performs SSA-aware copy propagation. Does not currently cross `if`
  boundaries; check whether this is intentional vs. an outstanding TODO.
- ~~Should treat `delay_assign` as opaque~~ — **done 2026-04-20.**
  `Opt_lnast::process_stmts` (`pass/lnastopt/opt_lnast.cpp:1075-1077`)
  now has an explicit no-op case for `Lnast_ntype_delay_assign` so the
  node silently short-circuits instead of hitting `process_todo` and
  printing a "not handling" warning. Matches the §15.2 semantics
  ("non-zero offsets are not a pure copy"): folding across
  `delay_assign` is never safe, so `lnastopt` simply leaves the node
  alone.

### 7.3 `upass/*` (work in progress)

- ~~`upass_constprop.cpp:21` strips a leading `%` only if the lhs is
  literally `%out`~~ — **done**, now strips any `%`/`$` prefix.
- `upass_constprop.cpp:375-381` hardcodes the `__` attribute-skip
  predicate — see §4.1. Still pending.
- ~~`upass_core.hpp` `PROCESS_NODE` list missing `attr_set`, `attr_get`,
  `func_def`, `for`, `while`, `range`, `cassert`, `type_def`, `type_spec`~~
  — **done**.

### 7.4 `pass/upass` runner vs `upass/runner`

*Status:* **resolved 2026-04-20** — not actually duplicated, they are
layered. Both directories keep their current role:

- `pass/upass/` (≈180 LOC, `pass_upass.{hpp,cpp}`): EPRP plugin shell.
  Registers the `pass.upass` command, parses labels (`verifier`,
  `constprop`, `assert`, `order`, `max_iters`, `ir`, `dry_run`,
  `inherit`), and dispatches by IR mode (`lnast` → `uPass_runner`,
  `lgraph` → `uPass_runner_lgraph`).
- `upass/runner/` (≈900 LOC): core implementation. `upass_runner.cpp`
  contains the Lnast-path runner; `upass_runner_lgraph.cpp` contains
  the Lgraph-path runner. Both drive the iteration fixed-point over the
  registered sub-passes (`upass/constprop/`, `upass/lgraph_*/`, etc.).

The "duplicated setup code" note in the original TODO was a
misreading — `Pass_upass::setup()` is the EPRP label declaration,
while `uPass_runner` / `uPass_runner_lgraph` constructors resolve
the pass ordering and instantiate the sub-pass objects. No overlap.

---

## 8. Test infrastructure

*Status:* **pending** — can't usefully land the golden harness while slang
doesn't build.

1. Add a golden LNAST test per producer: run
   `inou.prp|inou.pyrope|inou.verilog … |> lnast.print` on a shared corpus
   and diff against `*.lnast.expected` files. Catches §1 drift regressions.
2. Convert `inou/pyrope/tests/lnast_prp_test.sh` into a bazel `sh_test` so
   regressions surface in `bazel test //...`.
3. `lnast/tests/ln/*.ln` uses its own surface syntax (see
   `lnast/tests/ln/types.ln`). Document this syntax — it's the LNAST textual
   serialization and is not the same as the `lnast.dump` output.

---

## 9. Suggested ordering

Updated after the 2026-04-19/20 session. Items crossed out are done.

Smallest blast-radius first:

1. Write this document's §3 snippets into `inou/prp/tests/comptime/` as
   parse-smoke tests so drift shows up immediately (no LNAST-shape work
   needed). **Pending.**
2. Land a golden-output harness in `pass/lnastfmt` / `lnast/tests`
   (§5, §8.1). Capture current behavior of all three producers as
   baseline. **Pending; blocked on slang build.**
3. ~~Pick one naming axis (recommend: input/output prefix — `$foo` vs
   `$.foo`) and unify across producers.~~ **Done.**
4. ~~Move SSA subscript out of the ref text (§4.3).~~ **Done 2026-04-20** —
   `Lnast::dump` renders `name|subs` now; see §A.
5. Resolve the `__attr` → bare-attr question (§4.1). Touches a lot of files
   but is mostly mechanical once lnastfmt can enforce. **Pending.**
6. Decide storage-class representation (§2, §3.1). Blocks the new-Pyrope
   front-end. **Pending.**
7. Everything else in §3 (match, timed ref, memories, etc.) — one PR per
   item, each gated on §5. **Pending.**

---

## 10. Plan: attribute propagation rules for the symbol-table rework

*Status:* **pending.** Not started.

Complements §1 and §4. Decides how attributes flow (or don't) once they live
in a side symbol table instead of in-band strings.

### 10.0 Model: "attributes don't propagate — values are computed"

- **Declarations own attributes.** The only place user/decl attributes are
  written is at the declaration site (`let`, `mut`, `const`, `reg`,
  `comptime`, port, lambda arg, tuple literal field). Examples of decl-only
  attrs: `storage` (const/mut/reg/comptime), `direction` (in/out),
  `reset_pin`, `clock_pin`, `initial`, `pipeline_depth`, `debug`, user-
  asserted bounds (`max=N`, `min=N`).
- **Assign/expressions do NOT propagate attributes.** `y = x` creates a
  fresh entity `y`. None of x's decl-only attrs travel to y. This matches
  hardware semantics (y is a new wire/net, not a view of x).
- **Value attrs `__min`/`__max` are computed, not propagated.** They are
  derived per-SSA-version from the RHS expression via interval arithmetic.
  `y = x` computes `y.__max = x.__max` as a trivial case of identity, not
  as a propagation rule. `z = a + 1` computes `z.__max = a.__max + 1`.
- **Tuple literals are the one structural exception.** In
  `a = (foo=b, c)`, field `a.foo` carries forward b's decl-only attrs, and
  the positional slot carries c's. This is because a tuple literal
  constructs structure that references the source entities per-field; it's
  not a flat expression.
- **Bit-width attrs (`bits`/`ubits`/`sbits`) are derived views over
  `__min`/`__max`** — not stored independently. Consumers that need a bit
  width query a helper that inspects min/max and the signedness attribute.

### 10.1 Two ST lifetimes

- **Per-declaration table** (small, written once, read often): decl-only
  attrs keyed by the pre-SSA declaration name (`a`, not `a___3`).
- **Per-SSA-version table** (larger, populated by the inference pass):
  `__min`/`__max` keyed by SSA name (`a___0`, `a___1`, ..., `___t5`).

### 10.2 Value-attr inference pass (eager)

- Runs as part of `pass.lnastfmt` (or a dedicated sub-pass invoked by
  lnastfmt) **after** SSA, **before** any consumer that needs widths.
- Walks LNAST once; for each SSA definition computes `__min`/`__max` from
  the RHS using interval arithmetic. Writes the result into the per-SSA
  ST entry.
- For regs: fixed-point over all RHS expressions assigned to the reg's SSA
  versions, unioned with the declared `initial` value. Converges quickly
  because the reg decl provides a user-asserted bound (or widens until the
  bound is hit, at which point lnastfmt errors if assigned values exceed
  the assertion).
- Constant-folds trivially along the way (does not replace `lnastopt` —
  just produces the ranges, not the folded tree).

### 10.3 User-asserted bounds vs derived bounds

- Asserted bound at decl (e.g., `mut a:[max=100]`) is stored as a
  decl-only attr on `a`. It is a constraint, not a fact about a specific
  SSA version.
- Derived `__max` is per-SSA-version, computed by §10.2.
- After each assignment, the inference pass must check:
  `a___i.__max <= a.max_asserted` and symmetric for min. Violations are
  lnastfmt errors (with source location from the LNAST token).
- Keep them as separate keys in the ST (`max_asserted` vs `__max`) to
  avoid ambiguity.

### 10.4 Consumer contract

- Decl-only attr lookup: `st.get_decl_attr(decl_name, key)` — returns the
  declaration's attr or absent.
- Value attr lookup: `st.get_value_attr(ssa_name, key)` — returns the
  per-SSA `__min`/`__max`. Must be populated by §10.2 before the consumer
  runs; lnastfmt errors otherwise.
- Direction / storage / reg-ness checks (replacing today's
  `front() == '$'` etc.) go through decl-only lookup: `is_input(name)` →
  `st.get_decl_attr(decl_of(name), direction) == input`.

### 10.5 Node/shape changes

- Every `ref` carries (or can be resolved to) its declaration name. Either:
  - Add a `decl_id` field on `Lnast_node` populated during parse, or
  - Keep using the pre-SSA text (before the `___<n>` suffix) as the key.
- The existing `Lnast_node::subs` field already separates SSA subscript
  from the text. §4.3 cleanup (stop concatenating `___<subs>` into the ref
  text) directly supports the ST keying scheme here.

### 10.6 Gating / ordering

- Land the ST data structure + API first (empty, unused).
- Populate from existing in-band strings in lnastfmt (both old `__ubits`
  and new bare `bits` forms). Consumers still read in-band.
- Migrate consumers one at a time to read via ST. Each migration gated
  on golden tests (§8.1).
- Stop emitting in-band strings from producers.
- Delete string predicates (`front() == '$'`, `starts_with("__")`, etc.).

---

## 11. Plan: unify `attr_set` with `tuple_set` syntax; collapse `assign`

*Status:* **pending.** Gated on §10.

Outcome of a design discussion on Issue 3 (path addressing) and §2's
`let`/`var`/`assign`/`mut` confusion.

### 11.1 Shared variadic-path shape

`attr_set` and `tuple_set` share the same child shape (root ref, tuple
path elements, terminal value), differing only in how the terminal pair is
interpreted:

- `tuple_set root  p1 p2 ... pN   value`
  Writes `value` into tuple slot `root.p1.p2…pN`.
- `attr_set  root  p1 p2 ... pN   attr_key   value`
  Writes `value` into the attribute named `attr_key` on tuple slot
  `root.p1…pN`. The **last non-value child is always the attribute key**;
  everything between root and attr key is tuple path. Resolves the path-vs-
  attr-key ambiguity (Issue 3) with a positional convention.

`attr_get` mirrors `attr_set` without the value — last child is the attr
key; everything before is the tuple path.

`tuple_get` unchanged.

### 11.2 Semantic difference: write-once, comptime-guarded

`attr_set` is allowed **anywhere in source code**, including inside control
flow. The constraints are on *evaluation*, not location:

- **Write-once per `(target, attr_key)` pair.** Across the comptime-resolved
  execution, each attribute slot is written at most once. Second write
  ⇒ lnastfmt error.
- **Guards must be comptime-resolvable.** Any enclosing `if`/`for`/`while`/
  `match` condition controlling an `attr_set` must be evaluable at compile
  time. Runtime-guarded `attr_set` ⇒ lnastfmt error.
- **Values must be comptime-resolvable.** The value passed to `attr_set`
  must be a const, a comptime variable, or a comptime expression.
- **No `attr_set` after a read.** Once a variable has been *read* in source
  order, any subsequent `attr_set` on that variable is a compile error.
  This prevents the "first half sees one attr set, second half sees
  another" bug class. A "read" is any use that observes the value or the
  attribute set: RHS of `assign`/`tuple_set`, operator argument,
  call-site argument, `if`/`while`/`match` condition, or target of an
  `attr_get`. Pure `tuple_set` (writes) do **not** freeze — they are
  validated against the final resolved attr set at comptime resolution.
- **Source-order independence among attr_sets and writes.** Provided no
  read has occurred yet, `attr_set` and `tuple_set` can interleave freely.
  Example legal sequence: `tuple_set a 5; attr_set a max 10` — the write
  is validated against `max=10` at comptime resolution. Illegal sequence:
  `b = a; attr_set a max 10` — the read of `a` has already occurred.
- **Source-order dependence for attr-reads inside attr values.** If
  `attr_set a max (1 << a.bits) - 1` reads `a.bits`, the corresponding
  `attr_set a bits ...` must evaluate first in source order. Reading an
  unset attr during the comptime phase ⇒ error. Keeps the comptime phase
  from needing topological sort.
- **Derived `__min`/`__max` are NOT emitted via `attr_set`.** The value-attr
  inference pass (§10.2) writes them to the per-SSA-version ST table
  directly; `attr_set` remains exclusively for user/decl-time attrs.
- **Unset attrs at use site are "absent", not error.** A consumer querying
  `a::[max]` when no `attr_set a max ...` ever ran (e.g., comptime-guarded
  branch was false) gets "absent". Consumers decide whether absence is
  meaningful (e.g., width inference falls back to `__min`/`__max`
  derivation).

Common patterns enabled:

```pyrope
comptime USE_PIPELINE = true
mut stage = 0
if USE_PIPELINE {
  attr_set stage storage reg
} else {
  attr_set stage storage mut
}

comptime N = 4
for i in 0..<N {
  attr_set bus.lane[i] bits 32   // each i targets a distinct slot
}
```

### 11.3 Collapse `assign` into `tuple_set`

`tuple_set` with two children (`root`, `value`) is semantically identical to
today's `assign`. Collapse:

- `assign root value` → `tuple_set root value` (canonical 2-child form).
- Remove `assign` from `lnast_nodes.def` (or keep as a deprecated alias for
  migration).
- `mut` (already `FIXME: remove`) → `tuple_set` + decl-time `attr_set ...
  storage mut`. Mutability lives in the attribute, not a node kind.
- `dp_assign` (also `FIXME: remove`) stays as its own concern — "deferred
  parent ownership" is a scope question, not a tuple-path operation.
  Separate issue.
- Producers, `lnast_create.cpp`, and consumers migrate in lockstep with the
  ST rollout.

### 11.4 Example lowerings

**Declaration with inline attrs and initial value:**

```pyrope
mut data:u32:[max=1000, min=0] = 0
```

```
attr_set data storage mut
attr_set data bits 32
attr_set data max 1000
attr_set data min 0
tuple_set data 0
```

**Nested-field attribute:**

```pyrope
mut config:[clock = (freq:u32:[max=1e9] = 100_000_000)]
```

```
attr_set config clock freq storage mut
attr_set config clock freq bits 32
attr_set config clock freq max 1_000_000_000
tuple_set config clock freq 100_000_000
```

**Register with reset pin:**

```pyrope
reg counter:u8:[reset_pin=rst, initial=0] = 0
```

```
attr_set counter storage reg
attr_set counter bits 8
attr_set counter reset_pin rst
attr_set counter initial 0
tuple_set counter 0
```

### 11.5 lnastfmt enforcement

Single-pass validation:
1. Track `(decl_name, attr_key)` pairs seen via `attr_set`. Duplicate ⇒
   error "attribute already set".
2. For each `tuple_set`/read, mark the decl as "in use phase". Any later
   `attr_set` on that decl ⇒ error "attribute set after first use".
3. Any `attr_set` encountered inside a non-decl scope (child of `if`,
   `for`, `while`, `func_def`, `stmts` nested under control flow) ⇒ error.
4. Validate attr_key against the known-attr whitelist. Unknown keys ⇒
   warning during migration, error post-migration.

### 11.6 Files affected

- `lnast/lnast_nodes.def` — add/confirm `attr_set` as variadic; mark
  `assign`/`mut` deprecated.
- `lnast/lnast.cpp`, `lnast/lnast_create.cpp` — emit `tuple_set` for the
  assign case; emit `attr_set` with path-value form.
- `lnast/bundle.cpp:736-956` — replace the `__`-prefix canonicalization
  with ST-backed attribute lookup.
- `inou/prp/prp2lnast.cpp`, `inou/pyrope/prp_lnast.cpp`,
  `inou/slang/slang_tree.cpp` — producers emit unified form.
- `pass/lnastfmt/pass_lnastfmt.cpp` — add validations §11.5.
- `pass/lnast_tolg/*`, `inou/code_gen/code_gen.cpp`, `upass/*` —
  consumers read via ST API; stop string-matching `__xxx`.

### 11.7 Ordering

- Gated on the ST API from §10 landing first.
- Can progress in parallel with §15 (`delay_assign` / `__create_flop` →
  attribute) — orthogonal node-shape changes.

---

## 12. Plan: replace `$`/`%`/`#` prefixes with ST-backed direction/storage

*Status:* **pending.** Gated on §10 + §13 full.

Remove prefix-in-ref-text as in-band signaling; move direction/storage to the
symbol table as decl-only attributes.

### 12.1 Representation

- **Stored ref text carries bare names only** — no `$`/`%`/`#` prefix, no
  dotted variant.
- Direction and storage are decl-only attributes set via `attr_set`:
  - `attr_set foo direction input` (replaces `$foo`)
  - `attr_set foo direction output` (replaces `%foo`)
  - `attr_set foo storage reg` (replaces `#foo`)
- Consumers query via ST helpers: `st.is_input(name)`, `st.is_output(name)`,
  `st.is_reg(name)`. The string-prefix tests (`front() == '$'` etc.) go
  away.

### 12.2 `lnast.print` rendering (LLVM-IR-style)

`lnast.print` synthesizes prefixes from the ST on dump for human
readability:
- `$foo` for inputs, `%foo` for outputs, `#foo` for regs, bare name
  otherwise.
- Tmps can render LLVM-style as `%<tree_index>` once §13 lands.
- SSA subscripts render as `foo.<n>` or similar (see §4.3 on moving SSA
  subscript out of ref text).
- Round-trip is not a goal — `lnast.print` is for humans, not parsing.

### 12.3 Top-level I/O bundle

- `pass/lnast_tolg/lnast_tolg.cpp:40` uses bare `"%"` as the output-bundle
  sentinel. Drop the sentinel: lnast_tolg iterates decls with
  `direction=out` from the ST.
- Similarly for input-bundle lookups.

### 12.4 Clock / reset / other implicit signals

- No reserved names. Clock and reset are ordinary inputs (`attr_set clk
  direction input`), with the reg's `clock_pin` / `reset_pin` attributes
  pointing to them by name (`attr_set counter clock_pin clk`).
- Removes the special-case handling for `$clk`/`$reset`-like conventions.

### 12.5 Migration order

1. Land ST API + `is_input` / `is_output` / `is_reg` helpers (initially
   populated by falling back to the prefix). No consumer changes yet.
2. Migrate consumers one at a time to use the helpers. Each migration
   gated by the golden-output tests (§8.1):
   - `pass/lnast_tolg/*`
   - `inou/code_gen/code_gen.cpp`
   - `upass/*`
   - `lnast/bundle.cpp`
3. Migrate producers to emit bare names + `attr_set`:
   - `inou/prp/prp2lnast.cpp:1227-1229` — drop dotted form, emit attrs.
     (Dotted form already removed.)
   - `inou/pyrope/prp_lnast.cpp` — all `$`/`%` call sites.
   - `inou/slang/slang_tree.cpp:83,90` — I/O wiring.
4. Remove prefix-parsing in `lnast.hpp:168-170` / `lnast.cpp` `is_input`
   etc. Dump code renders from ST.

### 12.6 Files affected

- `lnast/lnast.hpp:168-170` — `is_input`/`is_output`/`is_reg` predicates
  forward to ST.
- `lnast/lnast.cpp` — remove prefix-based string tests.
- `lnast/lnast_writer.cpp` — ST-backed rendering.
- Producers: `inou/prp/prp2lnast.cpp`, `inou/pyrope/prp_lnast.cpp`,
  `inou/slang/slang_tree.cpp`.
- Consumers: `pass/lnast_tolg/*`, `inou/code_gen/code_gen.cpp:842+`,
  `upass/upass_constprop.cpp:21,375-381`, `lnast/bundle.cpp`.

### 12.7 Gating

Depends on §10 (ST API) and §11 (`attr_set` semantics) being in place.
Largest mechanical change — should be the last major cleanup, not the
first.

---

## 13. Plan: tmp vars use `Tree_index`; foreign names sanitized per-producer

*Status:* **partial.** String-prefix drift resolved (§1.1); `Tree_index`
identity pending.

### 13.1 Tmp variables

- Compiler-generated tmps carry **no ref text**. Their identity is
  `Lnast_node::Tree_index`. **Pending.** Today all three producers emit
  the canonical `___<n>` string.
- A tmp is an `Lnast_node` with kind `ref` and a tmp-marker (bit on the
  node, or a dedicated leaf kind — decision during impl).
- When one operator's output feeds another's input, they reference each
  other via `Tree_index`, not by name.
- `lnast.print` renders tmps LLVM-style: `%<tree_index>` in dumps.
- `pass_lnastfmt.cpp:204` `is_temp_var` string-match goes away; becomes
  `node.is_tmp()`.

### 13.2 SSA interaction

- Tmps are SSA by construction (single-assignment by tree position); the
  SSA pass does not rename them.
- `lnast.cpp:1229`'s `starts_with("___") → skip SSA` guard becomes
  unnecessary.

### 13.3 Foreign-language identifier safety

LNAST stored ref text is restricted to the "safe identifier" grammar:
`[A-Za-z_][A-Za-z0-9_]*`. Producers for source languages with looser
rules must sanitize and preserve the original.

**Rule:** any identifier that does not match the safe grammar is rewritten
to a safe form by the producer, and the original name is stored via
`attr_set <safe_name> source_name "<original>"`.

Examples:
- Verilog escaped `\foo-bar` → safe `foo_bar` (or `foo_bar_<hash>` for
  collision avoidance) + `source_name="\foo-bar"`.
- Digit-leading `003` → safe `v003` + `source_name="003"`. (Verilog
  producers are free to follow Yosys's `$003` convention if preferred —
  that's a producer-internal sanitization choice, not an LNAST-level
  requirement.)

**Why not keep Yosys's `$`/`\` convention in LNAST directly?** Under §12,
`$` is reserved by `lnast.print` for input rendering. Allowing `$`-
leading stored text would either collide with that convention or force
multi-char escape in dumps. Cleaner to keep LNAST text always-safe and
push producer-specific conventions into the sanitizer layer.

**Emitting back to the source language:** emitters (e.g., a Verilog
backend) read `source_name` from the ST when present to reconstruct the
original identifier.

### 13.4 Benefits

- Eliminates cross-producer tmp collisions (only one tmp representation:
  `Tree_index`).
- Faster SSA (no string allocation per tmp, no substring checks).
- Clean handling of arbitrary source identifiers without collisions with
  LNAST-internal naming.
- Dump output is shorter (`%5` vs `___t5`).

### 13.5 Migration

1. Add `Lnast_node::is_tmp()` and `tmp_ref(Tree_index)` API.
2. Producers stop generating `___t<n>` strings; emit tmp refs by tree
   position.
3. Add a sanitizer in `inou/slang` (and future `inou/verilog`) that
   rewrites non-safe names and emits `source_name` attrs.
4. Consumers that look up tmps by name get a migration helper mapping old
   string forms to `Tree_index` — temporary, removed once producers are
   migrated.
5. Regenerate `lnast/tests/ln/*.ln` golden files.
6. Delete the `___*` prefix generators and the associated
   `substr`/`starts_with` tests from `lnast.cpp:988,1229`,
   `pass_lnastfmt.cpp:204`, and producer code.

### 13.6 Ordering

Independent of §10–§12. Recommended **before** §12's prefix cleanup
because it reduces the string-matching surface area §12 has to audit.

---

## 14. Plan: migration rollout for §10–§13

### 14.1 Per-producer strategy

- **`inou/prp` (new tree-sitter-based).** About to be rewritten to target
  the new `tree-sitter-pyrope` grammar. Emits the new LNAST form from day
  one. No legacy-shape support needed. Breaking semantics during the
  rewrite is fine.
- **`inou/pyrope` (legacy PRP parser, on sunset per §6.2).** Keep it
  functional only long enough for `inou/prp` to cover its test corpus.
  Migration at emission sites only:
  - Where it emits `tuple_set X __<attr> V`, emit `attr_set X <attr> V`.
  - Where it emits a `$X` / `%X` / `#X` prefix, emit bare `X` + the
    corresponding `attr_set X direction/storage ...`.
  - Skip the harder migrations: `__last_value` → `delay_assign` (§15),
    tmp-vars → `Tree_index` (§13). The tests exercising these keep the
    legacy form on this producer until it is deleted.
- **`inou/slang`.** Owned locally; update emission sites directly to the
  new form. No dual-mode needed. (Currently blocked by the fmt/clang-21
  toolchain issue — §E.)

No producer runs in dual-mode. Each flips atomically in its own PR. The
consumer side (reading via ST) is already ready when the flip lands.

### 14.2 Consumer migration

- Land the ST API (§10) with a populator that reads the **current** in-
  band form in `pass.lnastfmt` (legacy reader). Consumers switch to ST
  helpers one at a time.
- Each consumer migration (one PR each): `lnast_tolg`, `code_gen`,
  `upass_constprop`, `bundle`, `lnastopt`.
- Between PRs, master is green: consumers that have flipped use the ST;
  those that haven't still use string tests. Both paths are exercised by
  the golden-output harness (§8.1).

### 14.3 Legacy-form reader lifetime

`lnastfmt`'s legacy reader (populating the ST from `$foo` / `__bits` /
`__create_flop` / `___t<n>`) stays alive as long as `inou/pyrope` does.
When `inou/pyrope` is deleted, the legacy reader goes with it. At that
point, all producers emit canonical form and the ST is populated directly
from `attr_set` nodes.

### 14.4 Ordering

- **§13 (Tree_index tmps)**: land first. Smallest blast radius, no ST
  dependency. Only `inou/prp` and `inou/slang` migrate here; `inou/pyrope`
  keeps `___<n>` until deletion.
- **§10 (ST API + value-attr inference)**: second. Unblocks everything.
- **§11 (attr_set/tuple_set shape + collapse assign)**: third. Depends on
  §10.
- **§12 (prefix → direction/storage attrs)**: fourth. Depends on §10 + §13.
- **§15 (last_value/create_flop)**: any time after §10. Orthogonal to the
  other tracks; small diff. (§15.2 node landed; §15.1 `__create_flop` →
  attr still open.)

### 14.5 Golden-output safety net

`pass.lnastfmt` grows into a validator + normalizer (§5). Before any
change, snapshot the current three-producer output for the test corpus
into `*.expected` goldens. Each migration PR:

1. Runs `producer | lnastfmt | lnast.print` and diffs against goldens.
2. For PRs that intentionally change goldens (e.g., fixing §1 drift),
   call out the diff in the PR description and re-bless.

Acceptance criterion for "migration complete": all three producers emit
byte-identical LNAST for a shared canonical corpus (small-Pyrope).

### 14.6 Risk: goldens codify current bugs

The current `__bits`/`__ubits` split and prefix conventions are
inconsistent (per §1). The initial golden baseline will encode those
inconsistencies. Expect ~10% of golden diffs during migration to be
"this was wrong before; it's right now" — requires reviewer judgment.

---

## 15. Plan: replace `__last_value` / `__create_flop` magic constants

*Status:*
- §15.2 `delay_assign` node **landed** (offset=1 lowered via a wire
  placeholder). Both `inou/pyrope` and `inou/slang` producers emit
  `delay_assign` now — the old `attr_get X __last_value` form is gone.
  §15.2 extensions (offset=0 reg Q, offset=-1 past cycle, ref offsets,
  path-aware deferred reads) are still pending.
- §15.1 `__create_flop` → sticky attribute **landed 2026-04-20** (see
  details below).

### 15.1 `__create_flop` → sticky attribute — DONE

- The `__create_flop` magic string is gone from both producer emission and
  consumer string-matching. Register declaration now flows through the
  existing `attr_set` node with shape `(ref X, const "storage",
  const "reg")`.
- Consumer dispatch lives in
  `pass/lnast_tolg/lnast_tolg.cpp process_ast_attr_set_op`: when the
  collected field path is `"storage"` and the value const is `"reg"`, a
  `Ntype_op::Flop` is created and registered in
  `driver_vname2wire_nodes` so subsequent `:=` writes feed its `din` pin —
  same downstream wiring as the old `__create_flop` branch.
- Producer changes:
  - `inou/pyrope/prp_lnast.cpp eval_tuple_dot_notation` intercepts the
    `field == "__create_flop"` case and emits the `attr_set` directly.
    The outer `x = x.__create_flop` collapses to a self-assign `x = x`,
    which is threaded through SSA harmlessly (the Flop output mapped in
    name2dpin propagates to later SSA subscripts).
  - `inou/prp/prp2lnast.cpp` already does not emit `__create_flop`.
  - `inou/slang` already goes through `Lnast_create` — no change needed.
  - `pass/lnast_fromlg/pass_lnast_fromlg.cpp` (LG→LNAST) updated to emit
    the new `attr_set` form for any Flop/Fflop/Latch node.
- Round-trip support: `inou/code_gen/code_gen.cpp do_attr_set` renders
  `attr_set X storage reg` back to the legacy surface `X = X.__create_flop`
  so `inou.code_gen.prp` output re-parses cleanly through the pyrope
  producer. Other attr_set shapes render as `X.<field> = <value>`.
- Dead code remaining (safe to remove in a follow-up):
  `lnast/lnast.cpp:145` still names `__create_flop` inside an `#if 0`
  block in `is_scalar_attribute_related`.
- **`#` prefix removal (part of §12) intentionally not done in this
  change** — §12 is gated on §10 (ST API) per §14.4 ordering, and
  unwinding `#` from `lnast/bundle.cpp`, `lgraph/lgtuple.cpp`, and
  `inou/code_gen/lnast_generic_parser.cpp` without the ST in place would
  require a throwaway register-set table on `Lnast`. Deferred until §10.

### 15.2 `__last_value` → new `delay_assign` statement

**Node shape** (now in `lnast/lnast_nodes.def`):

```
delay_assign:
  child 0: ref    (dst — always a fresh tmp)
  child 1: ref    (src — a declared variable name)
  child 2: const | ref   (offset — must resolve to a comptime constant)
```

**Semantics (target):**

- Positive offset = future / deferred read. Negative offset = past cycle.
- Offset `0` is legal **only if `src` is a reg**, and reads the flop Q pin
  (pre-update value at cycle start). For a non-reg, offset `0` is a compile
  error — lnastfmt rejects.
- Offset `1` on a reg reads the D / next-cycle value (the `::[defer]` case
  today encoded as `__last_value`). Offset `1` on a wire reads the settled
  end-of-block value.
- Offset operand may be a `const` literal or a `ref` to a comptime-const
  variable (e.g., `xx = 3; a@[xx]`), but must *not* depend on runtime input.
  Resolution happens during constant folding; lnastfmt flags if the offset
  is still non-const after folding.
- Surface mapping:
  - `a::[defer]`  → `delay_assign tmp a 1` (non-reg) or `1` (reg = next-cycle D)
  - `a@[N]`       → `delay_assign tmp a N`
  - `a@[-1]`      → `delay_assign tmp a -1`
  - `a@[]`        → `delay_assign tmp a 1`  (shorthand for defer)

**Semantics (currently implemented):**

- Offset=1 on a wire: lowered by `process_ast_delay_assign_op` as a wire
  placeholder (Or-identity node), mirroring the old `__last_value` path.
- Any other offset: `Pass::error` — full offset semantics still pending.

**SSA rules:**

- `dst` is always a fresh tmp; the SSA pass does not subscript it further.
- `src` references the **pre-SSA declaration name**. Today this works
  because `get_vname` returns token text (pre-SSA) and the SSA subscript
  lives in `Lnast_node::subs` separately — but the TODO's intent of
  "SSA pass must skip child 1 of `delay_assign` when renaming" should be
  explicitly enforced once tmp identity switches to `Tree_index` (§13).

**Usage pattern (replaces today's `__last_value` chain):**

```
// before:
//   attr_get ___t0  a  __last_value
//   func_call a_q  __flop  ... din=___t0 ...
//
// after:
delay_assign ___t0  a  1
func_call a_q  __flop  ... din=___t0 ...
```

**Files affected:**

- `lnast/lnast_nodes.def` — `delay_assign` **added**.
- `lnast/lnast.cpp:145,161,221,377` — `__last_value` magic string **still
  referenced**; can be removed once all producers migrate.
- `lnast/lnast_create.cpp:28-34` — **emits `delay_assign`** (done).
- `inou/prp/prp2lnast.cpp` — **pending**: emit `delay_assign` for
  `::[defer]`, `@[N]`, `@[-1]`, `@[]` (none of these work today).
- `inou/pyrope/prp_lnast.cpp:1631` — **pending**: still emits
  `attr_get ... __last_value`.
- `inou/slang/slang_tree.cpp` — uses `lnast_create.cpp` path; inherits
  the migration.
- `pass/lnast_tolg/lnast_tolg.cpp` — `process_ast_delay_assign_op`
  **added** (offset=1 only).
- `pass/lnastopt` — **pending**: make `delay_assign` opaque to copy-prop
  (non-zero offset is not a pure copy).
- `pass/lnastfmt/pass_lnastfmt.cpp` — **pending** validations:
  - child 0 must be a tmp ref
  - child 1 must be a declared variable ref (not a tmp, not a literal)
  - child 2 must be comptime-const after folding; reject runtime refs
  - offset `0` only when `src` has `storage = reg`
- `upass/*` — **added** `process_delay_assign` hook in
  `upass/core/upass_core.hpp`.

**Test coverage** (still to add to `lnast/tests/ln/`):

- reg Q-pin read at offset 0
- defer on a wire (offset 1, non-reg) ← currently the only lowered case
- past cycle on a reg (offset -1)
- comptime-ref offset (`xx = 3; a@[xx]`)
- reject: offset 0 on a wire
- reject: runtime-ref offset

**Ordering:** independent of §15.1 and the broader §1/§4 cleanup, since it
only adds a node. The lowering dispatch on `storage = reg` will initially
co-exist with the old `__create_flop` until §15.1 lands.

---

*Anything added here should come with a file:line pointer so the next pass
through can tell what's still true.*
