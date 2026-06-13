# Uncalled LiveHD C++ functions — triage RESOLVED (2026-06-12)

Coverage triage of zero-call (`FNDA:0`) LiveHD functions. The candidates below
were actioned: genuinely-dead code was deleted; functions referenced by live
code (untested, not dead) were kept, with one new regression test added.

Verified after the change: `bazel build -c dbg //...` green; `bazel test //...`
green (597/599 — the lone failure, `inou/slang:slang_compile-add`, is a
pre-existing flaky yosys-LEC crash that passes on rerun, unrelated to this diff).


### 1. Dead dispatch — retired grammar rules → **DELETED**

`inou/prp/prp2lnast.cpp` — `process_assert_statement`,
`process_function_call_statement` (function bodies, header decls, and their
`stmt_dispatch` table entries). The pinned grammar
(`tree-sitter-pyrope@279462b`) no longer emits `assert_statement` /
`function_call_statement` node types — `assert(...)` and a bare `f(...)` parse
as `function_call_expression` and route through the expression-statement path —
so both handlers were unreachable.


### 5. Diagnostic/error-emitter template instantiations → **KEPT (unchanged)**

One source template each (`Lnast::info<…>`, `upass::error<…>`, `fmt_error<…>`,
`parser_error<…>`, …); covering every instantiation would mean reproducing
every diagnostic permutation. No action.


### 6. Unused API overloads / alternate entry points → **DELETED where dead, KEPT where live**

DELETED (grep-verified zero callers):
- `pass/common/eprp_var.cpp` — `add(const Eprp_dict&)`, `add(Eprp_lnasts&)`,
  `add(const Eprp_var&)`, `add(std::unique_ptr<Lnast>)`, and both `replace`
  overloads. `lhd` uses only `add(shared_ptr<Lnast>)`, `add(shared_ptr<Graph>)`,
  and `add(name, value)` (all kept).
- `lnast/lnast.cpp` — `read`/`read_all` (2 each), the anon `finish_read` +
  `is_name_line` helpers they depended on, `set_root(const Lnast_node&)` (the
  `Lnast_ntype_int` overload is kept), and `spans_of`. The textual-dump
  read-back path is unused; `ln:` reload goes through `hhds::Tree::read_dump`.
- `lnast/lnast_builder.cpp` — `create_red_{and,or,xor}_stmts`,
  `create_{mask,bitmask,mod,declare_bits}_stmts`, `create_func_call` (the
  builder method, **not** the `Lnast_ntype::create_func_call` factory),
  `create_tuple_get` (2-arg overload). Plus the now-dead naming helpers
  `get_lnast_name` / `get_lnast_lhs_name` / `mark_input_name` and the
  `vname2lname` / `input_lnames_` fields they used (the map was never even
  populated). The 1-arg `create_tuple_get` is **kept** — referenced by the
  live `create_assign_stmts` compound-path branch.
- `upass/core/bundle.cpp` — `concat(const Dlop&)` and
  `get_bundle(const shared_ptr<Bundle const>&)`. The `string_view` `get_bundle`
  and the `(shared_ptr, conflict)` `concat` are live and kept.
- `upass/func_extract/upass_func_extract.cpp` — `strip_io_prefix` (the
  identically-named `uPass_coalescer::strip_io_prefix` is a different, live
  function — not touched).
- `upass/attributes/upass_attributes_wrap_sat.cpp` — `Const_handler::on_attr_set`,
  a pure no-op that overrode the empty base no-op. Removed together with the
  `Const_handler` class and its `reg.register_exact("type", …)` registration;
  `lookup("type")` now falls back to the default no-op handler — behavior is
  provably identical, because the real `type=const` single-bind enforcement
  lives in `record_assign` (untouched). Verified: every `*const*` /
  `*reassign*` / `*redeclare*` error test still passes.

KEPT (referenced by live production code — FNDA:0 is a coverage artifact, not
deadness; removing them would break the build / silently change behavior):
- `upass/runner/upass_runner.hpp` `take_new_lnasts` — an inlined one-line
  `override` consumed by `pass_upass.cpp`; runs on every func-extract /
  template-specialization test, but inlining leaves no out-of-line symbol to
  attribute hits to.
- `upass/runner/upass_runner.cpp` `try_scalar_kind` — reachable from the comb
  inliner; already exercised by `inou/prp/tests/comptime/overload_kind_var.prp`
  (the comptime suite was simply outside the coverage subset that produced
  this list).
- `pass/cprop/cprop.cpp` `try_find_single_driver_pin` — cprop's runtime
  Set_mask-chain single-bit resolver, genuinely reachable at `--recipe O1/O2`
  but not previously exercised by any test. **New regression test added:**
  `lhd/tests/lhd_setmask_bitread_test` (+ `lhd/tests/setmask_bitread.prp`)
  compiles a runtime bit-write chain at O1 and asserts the single-bit reads
  fold (`y0 -> a`, `y2 -> c`), which only happens via this resolver.


### 7. Graph color/serialize helpers — graphviz/attr-emit gated → **KEPT (unchanged)**

`graph/node_util.hpp` `color_of` / `is_sink_connected` /
`set_type_const_serialized`; `graph/const_pin.cpp` `hydrate_const`. Consumed by
`inou/graphviz` and `inou/attr`, which become reachable once the
graphviz/attr egress lands. No action.
