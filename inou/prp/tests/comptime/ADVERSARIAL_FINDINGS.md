# Adversarial Pyrope comptime findings


## Methodology

1. **Generated** 1250 valid Pyrope `comptime` test programs (59 feature-area agents), each
   intended to compile cleanly with every top-level `cassert(...)` true.
2. **Triaged** deterministically: `lhd compile <f> -q --set upass.verifier=true`.
   `rc==0` => the compiler agrees (test deleted); `rc!=0` => kept as a candidate failure.
   986 passed (deleted), 264 failed.
3. **Validated** each failure with a skeptic agent that re-read the docs and judged
   VALID_BUG vs INVALID_TEST: **215 VALID_BUG / 9 INVALID_TEST**.
4. **Deduplicated** to ~100 representatives across the 47 families below (`adv_*.prp`).
   Verdict definition: a "valid bug" is code that is unambiguously valid per
   `~/projs/docs/docs/pyrope/*.md` yet crashes / errors / folds a true `cassert` false.

## Crash signatures (C++ asserts / SIGABRT on documented-valid code)

| assert | site | family | example |
|--------|------|--------|---------|
| `src2 >= 0` in `shln` | `blop.hpp:150` | bitsel_neg_range_crash | `b#[1..=-1]` and other negative-end bit ranges (doc 04-variables.md:308-313) |
| `dest_sz >= 1` in `addn` | `blop.hpp:48` | shared_io_name / inliner_addn_crash | chained call of `comb f(x)->(x)`; two identical-arg calls inlined |
| `"Multi-word division beyond 64-bit not yet implemented"` in `divn` | `blop.hpp:504` | bignum_ops | `(1<<100)/2`, `1e24/1e12` |
| bitwidth-range assertion | `upass_runner.cpp:942` | bignum_ops | `#+[0..=127]` popcount over a 128-bit all-ones value |
| SIGABRT | (arith right shift) | bitsel_neg_range_crash | arithmetic `>>` of a negative value by a large amount |

## Doc-confirmed highlights (I verified these against the spec myself)

* **single-bit `#[N]` returns `-1` instead of `1`** (28 tests). 04-variables.md:314 `cassert(b#[0]==1)`;
  lines 726/729: default bit-select is `zext`, "always positive results". The compiler sign-extends a
  set bit to `-1`. Largest family.
* **code-block-as-expression doesn't yield its last expression** (25 tests). 05b-statements.md and the
  quick-intro example `mut a = {mut d=3; d+1} + 100`. `const a = {mut d=3; d+1}` folds to 0/nil.
* **`comb f(x) -> (x)`** (shared input/output name) is explicitly legal (06-functions.md:255-257, :610)
  but the body reads the output not the input, and chained calls hit the `addn` assert.
* **`1 << (a,b,c)`** one-hot tuple-shift does not fold (skill/04-variables.md:568 `== 0ub01_1010`).
* **`mut x:[] = nil` + `++=` array-builder** (05b-statements.md:282-301,364-370) rejected as `nil-shape-infer`.
* **K/M/G/T scaled literals** (`1K==1024`, 02-basics.md:29-36): comptime decoder
  (`hlop/dlop.cpp` `Dlop::init_from_pyrope`) has no suffix handling — throws "1K encoding could not use K",
  folds wrong, or makes the value non-integer. (`doc_basic1.prp` is `:type: parsing lnast` so never caught it.)

## Notes / caveats for review

* **`reduce-returns-bool` family DROPPED** (3 tests): docs (04-variables.md:722-726) say reduces return
  `int{0,1}` "(not boolean)", but the existing `bitreduce.prp` relies on `== true/false`. Impl & existing
  test contradict the doc; tests asserting the documented `int` behavior were dropped to avoid contradicting
  established behavior. Worth a doc-vs-impl decision.
* **`~&`/`~|`/`~^`** (nand/nor/xnor): the docs use `~&` but the grammar implements `!&`/`!|`/`!^`. Kept as a
  doc-vs-impl operator mismatch (`nand_nor_xnor_ops`), not a clean compiler bug.
* **9 INVALID_TEST** correctly rejected by validation (genuinely-false casserts, non-mutually-exclusive
  `match` arms relying on if/elif priority, undocumented cross-type enum `in`).
* **Pre-existing flake (not from this run):** `overload_kind_var.prp` failed once with SIGTRAP (`rc=-5`)
  under parallel triage but passes 5/5 standalone and in clean re-runs — an intermittent non-deterministic
  crash (cf. the known `dlop_free_pool` dtor-order / `flat_hash_map` self-alias bugs).

Raw data: `/tmp/prp_adv/` (`triage.jsonl`, `verdicts.json`, `families.json`, generated corpus under `gen/`).


| # | family | total tests | reps kept | example root cause |
|---|--------|-------------|-----------|--------------------|
| 1 | `bitsel_single_bit_neg1` | 28 | bitsel_idx_12, bitsel_idx_18, bitsel_sext_04 | single-bit #[N] sign-extends to -1 |
| 2 | `codeblock_expr_no_fold` | 25 | code_blocks_02, unknown_bits_21, code_blocks_11 | code-block-as-expression does not yield its last expression (folds to 0/nil) |
| 3 | `tuple_append_noop` | 17 | tuple_append_03, tuple_append_10, tuple_concat_14 | field-path ++= appends duplicate of existing element instead of new value |
| 4 | `bitsel_neg_range_crash` | 9 | shifts_13, bitsel_sext_01, bit_assign_15, bitsel_slice_02 | arithmetic right shift of negative value by large amount aborts (SIGABRT) |
| 5 | `paren_unary_type` | 9 | cmp_bool_02, int_prec_06, bitwise_02 | parenthesized unary not/! loses boolean type tag in == |
| 6 | `enum_hier_encoding` | 9 | enum_hier_05, enum_hier_15, enum_hier_02 | hier-enum nested leaf identity wrong (value+string collide) |
| 7 | `loop_break_continue` | 9 | loops_while_02, loops_for_02, loops_while_13 | loop{...break} (do-while) unrolls body only once |
| 8 | `enum_identity_lost` | 8 | enum_cast_12, match_locals_19, enum_cast_19 | string-cast enum loses name-identity: ==/match folds false though int value correct |
| 9 | `match_relational_arm` | 8 | match_all_08, match_all_18, match_all_20 | match relational arm (</>/<=/>=) lowered as equality |
| 10 | `is_operator_false` | 6 | bool_cast_20, equals_is_has_in_03, does_op_15 | `is` operator folds false despite identical typenames |
| 11 | `enum_seq_numbering` | 6 | enum_seq_03, enum_hier_10, enum_seq_11 | integer-type enum ignores sequential-numbering trigger (uses one-hot) |
| 12 | `array_nil_shape` | 5 | array_build_02, loops_while_04, mix_loop_accum_14 | `mut x:[] = nil` accumulator: nil-shape-infer rejects ++= tuple build despite :[] decl |
| 13 | `string_int_ascii` | 5 | bitsel_slice_11, bitwise_19, casts_22 | string(int) ASCII typecast does not produce the character string |
| 14 | `shift_tuple_onehot` | 5 | bitwise_18, int_prec_16, shifts_06 | shift-by-tuple RHS (1<<(a,b,c)) not implemented |
| 15 | `shared_io_name` | 4 | arg_naming_18, comb_basic_06, multi_out_05, tuple_destruct_14 | same-name output `-> (x)` returns input not assigned output (chained call aborts blop.hpp  |
| 16 | `misc:comptime_[...]_param_slot_in_lambda_decl` | 4 | comptime_param_01 | comptime [...] param slot in lambda declaration does not parse |
| 17 | `ref_wrap_sat` | 4 | ref_args_11, ufcs_21, init_ctor_18 | wrap/sat-qualified write to ref param not propagated back to caller |
| 18 | `bignum_ops` | 4 | int_bigprec_16, int_bigprec_03, int_bigprec_10, int_div_10 | multi-word (>64-bit) division not implemented (blop.hpp divn aborts) |
| 19 | `case_nil_wildcard` | 3 | unknown_bits_11, case_op_22 | case ignores defined-bit mismatch when pattern has unknown bits |
| 20 | `implicit_cast_decl` | 3 | casts_23, string_basic_18 | implicit cast at typed-decl site (mut x:T = otherTypeVal) not performed |
| 21 | `does_op` | 3 | does_op_08, overload_13, equals_is_has_in_09 | does on tuples with unnamed fields ignores name/position when names diverge (folds false-s |
| 22 | `init_dispatch` | 3 | mix_deep_fold_21, init_ctor_11, init_ctor_06 | inline anonymous-tuple init constructor not invoked on `= value` construction |
| 23 | `arg_naming_tuple` | 3 | overload_18, mix_generic_tuple_04, mix_generic_tuple_03 | compact named-field tuple value mis-bound to a tuple-typed param (fields flattened into ar |
| 24 | `bitsel_neg_range` | 2 | array_basic_06 | custom-range array index domain (signed/non-zero base) not modeled; lowered to positional  |
| 25 | `nand_nor_xnor_ops` | 2 | int_prec_14, bitwise_14 | parser rejects documented `~&`/`~|`/`~^` nand/nor/xnor operators |
| 26 | `enum_in_membership` | 2 | enum_hier_04, mix_enum_match_08 | enum `in` bitwise-AND membership not implemented |
| 27 | `generic_bool_param` | 2 | generics_20 | bool actual bound to generic type param mistyped as integer |
| 28 | `misc:scalar_mut_accumulator_in_for-(i,v)-in-e` | 2 | mix_loop_accum_11 | scalar mut accumulator in for-(i,v)-in-enumerate loop does not accumulate |
| 29 | `string_format_positional` | 2 | string_interp_19, string_basic_07 | string(fmt, args...) positional form does not fill {} placeholders from trailing args |
| 30 | `string_selector_write` | 2 | tuple_mut_12, tuple_access_16 | write through string selector t['name']=v on a named field is a silent no-op |
| 31 | `ufcs_ref_self` | 2 | ufcs_05, ufcs_14 | result-bound ref-self -> (self) UFCS call mutates the original receiver |
| 32 | `misc:single-bit_lhs_bit-assign_inside_for-loo` | 1 | bit_assign_06 | single-bit LHS bit-assign inside for-loop in function body is dropped |
| 33 | `misc:#sext[lo..=hi]_drops_sign-ext_when_range` | 1 | bitsel_sext_19 | #sext[lo..=hi] drops sign-ext when range bounds are non-literal |
| 34 | `misc:range#[..]_does_not_fold_to_one-hot_inte` | 1 | bitsel_slice_12 | range#[..] does not fold to one-hot integer |
| 35 | `inliner_param_collision` | 1 | comb_basic_11 | inliner mangled param collides with same-named top-level const |
| 36 | `inliner_addn_crash` | 1 | comptime_param_16 | inliner crash adding two identical-argument calls to same comb (blop addn dest_sz>=1) |
| 37 | `misc:enum(...)_string/tuple_splice_does_not_c` | 1 | enum_hier_14 | enum(...) string/tuple splice does not create spliced fields |
| 38 | `range_fit_div` | 1 | int_div_13 | range-fit check uses dividend range instead of quotient range for `/` |
| 39 | `match_local_rebind` | 1 | match_locals_12 | same-named match-local across separate closed scopes rejected as const rebind |
| 40 | `range_step` | 1 | mix_loop_accum_20 | range `step N` rejected as mixed-precedence parse error |
| 41 | `return_leak` | 1 | multi_out_07 | early bare return does not freeze later output; post-return assignment leaks |
| 42 | `string_as_chars` | 1 | string_basic_03 | string-as-tuple-of-chars equality not implemented |
| 43 | `string_escape_brace` | 1 | string_interp_04 | escaped brace \{ keeps the backslash instead of collapsing to literal { |
| 44 | `misc:tuple_++_concat_with_string-valued_named` | 1 | tuple_concat_18 | tuple ++ concat with string-valued named fields merges the string values instead of keepin |
| 45 | `tuple_destructure` | 1 | tuple_destruct_04 | named-tuple destructuring lowered positionally instead of by-name |
| 46 | `elif_decl_visibility` | 1 | unique_if_06 | elif-header declaration not visible to its own condition |
| 47 | `sat_envelope` | 1 | wrap_sat_06 | sat clamps to bits-envelope not declared max/min for non-power-of-two int(min,max) |
