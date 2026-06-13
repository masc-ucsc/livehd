
### 1. Dead dispatch — retired grammar rules → **delete**

`inou/prp/prp2lnast.cpp`
- `Prp2lnast::process_assert_statement(TSNode)`
- `Prp2lnast::process_function_call_statement(TSNode)`

The dispatch table (`prp2lnast.cpp:1091,1096`) keys these on tree-sitter node
types `assert_statement` and `function_call_statement`. The **pinned** grammar
(`tree-sitter-pyrope@279462b`, see `MODULE.bazel`) no longer defines those
rules — `assert(...)` and a bare `f(...)` now parse as
`function_call_expression` and route through the expression-statement path
(verified against the generated `src/node-types.json`). These two handlers and
their dispatch entries are unreachable and should be removed (or the grammar
restored to emit those nodes if the distinct handling is still wanted).


### 5. Diagnostic/error-emitter template instantiations → **keep (one source template each)**

These are per-argument-type instantiations of variadic diagnostic helpers;
each fires only when *that* error/warning is emitted with *those* operand
types. They are a single source template, so they carry low deprecation risk —
covering all instantiations would mean reproducing every diagnostic permutation.

- `lnast/lnast.hpp` `Lnast::info<…>` — 4
- `upass/core/upass_utils.hpp` `upass::error<…>` — 3
- `pass/lnastfmt/pass_lnastfmt.cpp` `fmt_error<…>` — 10 (+ `node_loc`)
- `pass/common/eprp.hpp` `parser_error<…>` — 2; `pass/common/eprp.cpp`
  `parser_info_int`, `parser_warn_int`

### 6. Unused API overloads / alternate entry points → **review for deletion**

- `pass/common/eprp_var.cpp` — `Eprp_var::add` (4 overloads: `Eprp_var const&`,
  `flat_hash_map`, `unique_ptr<Lnast>`, `vector<shared_ptr<Lnast>>&`) and
  `Eprp_var::replace` (2). `lhd` only uses `add(shared_ptr<Lnast>)` and
  `add(shared_ptr<Graph>)`; these overloads are an unused surface.
- `lnast/lnast.cpp` — `Lnast::read` / `read_all` (2 each) and the anon
  `finish_read`, plus `Lnast::set_root`, `Lnast::spans_of`. The textual-dump
  read-back path; `lhd`'s `ln:` reload goes through `hhds::Tree::read_dump`
  elsewhere. Confirm whether any planned reload uses these before deleting.
- `lnast/lnast_builder.cpp` (12) — `create_red_and/or/xor_stmts`,
  `create_mod_stmts`, `create_mask_stmts`, `create_bitmask_stmts`,
  `create_declare_bits_stmts`, `create_func_call`, `create_tuple_get` (2),
  `get_lnast_name`, `get_lnast_lhs_name`. Builder helpers for ops the slang/cgen
  front-ends emit only for SV constructs the suite doesn't reach; testable with
  targeted SV but currently cold.
- `upass/runner/upass_runner.hpp` `take_new_lnasts`; `upass_runner.cpp`
  `try_scalar_kind`; `pass/cprop/cprop.cpp` `try_find_single_driver_pin`;
  `upass/core/bundle.cpp` `concat(Dlop)`, `get_bundle`;
  `upass/func_extract/upass_func_extract.cpp` `strip_io_prefix`;
  `upass/attributes/upass_attributes_wrap_sat.cpp`
  `Const_handler::on_attr_set`.

### 7. Graph color/serialize helpers — graphviz/attr-emit gated → **wire when those emits land**

`graph/node_util.hpp` `color_of`, `is_sink_connected`,
`set_type_const_serialized`; `graph/const_pin.cpp` `hydrate_const`. Consumed by
