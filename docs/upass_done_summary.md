# upass Work Done Summary

This document is a consolidated record of what has been implemented so far in LiveHD `upass`.

## 1) Environment and Build Setup
- LiveHD workspace used: `/Users/jamalsyed/compPassProj/livehd`
- Tooling installed and verified:
  - `bazelisk`
  - Bazel runtime `9.0.0`
  - `bison 3.8.2` (via `PATH=/opt/homebrew/opt/bison/bin:$PATH`)

## 2) `pass.upass` Command Improvements
- Added labels:
  - `order` (explicit pass order list)
  - `max_iters` (iteration cap)
- Behavior:
  - `order` overrides boolean toggles.
  - `max_iters:0` is normalized to 1 with a diagnostic.

Primary files:
- `pass/upass/pass_upass.cpp`
- `pass/upass/pass_upass.hpp`

## 3) Runner and Scheduling Improvements
- Added dependency-aware pass order resolution.
- Added cycle/missing-dependency detection.
- Added fixed-point style loop:
  - per-iteration reset of pass change flags
  - convergence early-stop when no pass changes
  - max-iteration stop message
- Added per-iteration diagnostics:
  - resolved order
  - changed passes list
  - converged vs max-iter message
- Added shared runner contract for iteration/diagnostics:
  - `upass/runner/upass_runner_common.hpp` (`Runner_fixed_point`)
  - reused by both LNAST and LGraph runners

Primary files:
- `upass/core/upass_core.hpp`
- `upass/runner/upass_runner.hpp`
- `upass/runner/upass_runner.cpp`
- `upass/runner/upass_runner_common.hpp`

## 4) Pass-Level Fixes and Metadata
- Added dependency metadata:
  - `assert` depends on `constprop`
- Fixed false change reporting in constprop so convergence can occur correctly.
- Removed duplicate plugin registration warning source in `pass_upass.cpp`.

Primary files:
- `upass/assert/upass_assert.hpp`
- `upass/constprop/upass_constprop.cpp`
- `pass/upass/pass_upass.cpp`

## 5) Test Coverage Added
- Added/expanded tests under `pass/upass/tests` and runner unit tests.
- Key targets now include:
  - `//pass/upass:upass_smoke_suite`
  - `//upass/runner:upass_runner_cycle_test`
  - `//upass/runner:upass_runner_lgraph_test`

Smoke suite behavior covered:
- convergence
- dependency ordering
- unknown pass handling
- max-iteration handling
- `max_iters:0` normalization
- first-iteration no-op convergence
- order parse validation
- cycle command behavior

## 6) Phase 4 (LGraph) Kickoff Work
- Added IR abstraction scaffold:
  - `upass/core/ir_adapter.hpp`
  - `upass/core/lgraph_manager.hpp`
- Added read-only LGraph runner prototype:
  - `upass/runner/upass_runner_lgraph.hpp`
  - `upass/runner/upass_runner_lgraph.cpp`
- Added unit test for prototype traversal/type collection:
  - `upass/runner/upass_runner_lgraph_test.cpp`

What this prototype does:
- Traverses `Lgraph::fast()`
- Logs visited node type names
- Collects visited node type names for test/diagnostics
- Performs a first real LGraph primitive:
  - constant-fold candidate scan summary
  - reports: visited nodes, const nodes, arithmetic nodes, fold-candidate nodes
  - candidate rule (current): arithmetic node with at least one input and all connected inputs driven by constants

### New routing step completed
- `pass.upass` now supports `ir` label:
  - `ir:lnast` (default): existing LNAST upass flow
  - `ir:lgraph`: routes to read-only LGraph traversal runner
- Added command-path validation:
  - `ir:lgraph` errors clearly if no LGraph input is in the pipeline.
- LGraph runner now uses the same fixed-point diagnostics framework as LNAST runner.
- LGraph runner now has pass-dispatch scaffold with dependency ordering:
  - graph-side plugin API in `upass/core/upass_lgraph_core.hpp`
  - built-in graph-side passes:
    - `visit`
    - `fold_scan` (depends on `visit`)
    - `fold_tag` (depends on `fold_scan`, mutation-capable and opt-in)
  - `pass.upass ir:lgraph` default order is now `visit,fold_scan`

### First mutation-capable LGraph pass (guarded)
- Implemented `fold_tag` graph-side pass:
  - identifies fold candidates (arithmetic nodes with all connected inputs constant)
  - mutates node metadata by setting color tag (`color=7`)
  - reports mutation count and uses `mark_changed()` for convergence tracking
- This pass is not in default LGraph order; it runs only when explicitly requested:
  - `pass.upass ir:lgraph order:fold_tag max_iters:3`

### First behavior-preserving semantic LGraph mutation (guarded)
- Implemented `fold_sum_const` graph-side pass:
  - finds `sum` nodes with exactly 2 constant inputs
  - creates replacement const node with folded value
  - reconnects all old sum outputs to replacement const node
  - deletes old sum node
  - marks changed for fixed-point convergence
  - reports metrics:
    - `rewired_edges`
    - `new_const_nodes`
    - `deleted_nodes`
- This pass is opt-in:
  - `pass.upass ir:lgraph order:fold_sum_const max_iters:3`

### Neutral-constant semantic simplification (guarded)
- Implemented `fold_neutral` graph-side pass:
  - `sum(x,0) -> x`
  - `or(x,0) -> x`
  - `xor(x,0) -> x`
  - `and(x,0) -> 0`
  - `mult(x,1) -> x`
  - `mult(x,0) -> 0`
  - `or(x,x) -> x`
  - `xor(x,x) -> 0`
  - `and(x,x) -> x`
  - `and(x,1) -> x` when `x` is proven 1-bit
  - `or(x,1) -> 1` when `x` is proven 1-bit
- Reports rewrite counts:
  - bypass-to-driver rewrites
  - rewrites-to-const-zero
  - rewired edges
  - new const nodes
  - deleted nodes
- This pass is opt-in:
  - `pass.upass ir:lgraph order:fold_neutral max_iters:3`

### Shift/div semantic simplification (guarded)
- Implemented `fold_shift_div` graph-side pass:
  - `div(x,1) -> x`
  - `shl(x,0) -> x`
  - `sra(x,0) -> x`
  - `div(0,c) -> 0` when `c` is known non-zero constant
  - `div(c,c) -> 1` when `c` is known non-zero constant
- Reports rewrite counts:
  - rewrites-to-driver
  - rewrites-to-const-zero
  - rewired edges
  - new const nodes
  - deleted nodes
- This pass is opt-in:
  - `pass.upass ir:lgraph order:fold_shift_div max_iters:3`

### LGraph rewrite dry-run mode
- Added `dry_run` option to `pass.upass`:
  - `pass.upass ir:lgraph order:fold_sum_const dry_run:true max_iters:1`
- Implemented dry-run support for semantic rewrite passes:
  - `fold_sum_const`
  - `fold_neutral`
  - `fold_shift_div`
- In dry-run mode these passes:
  - report the same rewrite counters (`rewired`, `new_consts`, `deleted`)
  - do not rewire edges
  - do not create replacement const nodes
  - do not delete original nodes
- Pass logs include `dry_run:true|false` to make mode explicit in diagnostics.

### Guarded identity expansion follow-up
- Extended summary/log fields with `to_const1` for non-zero constant target rewrites.
- Added tests and fixtures for new guarded identities:
  - `lgraph_neutral.prp` now includes 1-bit `and/or` with constant `1`.
  - `lgraph_shift_div.prp` now includes `5/5` and `0/0` (to validate the guard: `0/0` is not rewritten).
- Important constraint discovered:
  - `Ntype_op::Sub` in LGraph is a subgraph-instance node, not arithmetic subtraction for this pass path.
  - Therefore `x-x -> 0` was not added as a direct `Sub` rewrite and needs a different lowering-aware pattern if we want it later.

### Parser/error-path hardening for invalid pass requests
- Added runner-level configuration error reporting for:
  - unknown pass name
  - dependency cycle
  - invalid dependency chain
- `pass.upass` now converts these into clean runtime command errors (no process abort trap) for both IR modes:
  - `pass.upass invalid pass configuration: ...`
  - `pass.upass invalid lgraph pass configuration: ...`
- Added explicit regression coverage for label carry-over semantics in command pipelines:
  - `upass_lgraph_label_carryover_test.sh`
  - verifies `dry_run:true` carries into the next `pass.upass` stage unless explicitly overridden.

### Pipeline inheritance control (`inherit:false`)
- Added `inherit` label to `pass.upass` (default `true`):
  - `inherit:false` resets sticky option state for the stage.
- Effective reset scope:
  - all `pass.upass` option labels are stage-local when `inherit:false`.
  - only labels explicitly present in that command stage are applied.
  - covered labels: `order`, `max_iters`, `ir`, `dry_run`, `verifier`, `constprop`, `assert`, `inherit`.
- Implementation note:
  - added stage-local label tracking in `Eprp_var` via `stage_dict`, populated by `Eprp_pipe` before merged-label execution.
- Added regression test:
  - `upass_lgraph_inherit_false_test.sh`
  - verifies inherited labels are reset and explicit labels are reapplied in later stages.

### Graph-side command-path test plugins
- Added graph-only cycle test plugins:
  - `__upass_lgraph_cycle_cmd_a`
  - `__upass_lgraph_cycle_cmd_b`
- These are used by shell tests to validate cycle diagnostics through `pass.upass` (not just unit tests).
- Added shell regression:
  - `pass/upass/tests/upass_lgraph_mode_test.sh`
  - uses `inou.pyrope |> pass.lnast_tolg |> pass.upass ir:lgraph`
  - checks visit logs and summary output
- Added negative-path shell regression:
  - `pass/upass/tests/upass_lgraph_missing_input_test.sh`
  - uses `inou.pyrope |> pass.upass ir:lgraph` (without `pass.lnast_tolg`)
  - checks that a clear missing-input error is emitted

What it does not do yet:
- No cross-IR shared pass object model (LNAST and LGraph still use separate pass plugin base types)
- No broad semantic fold coverage for all safe identities (only currently implemented guarded subset)

## 7) Validation Status
Most recent validation results:
- `bazel test //upass/runner:upass_runner_cycle_test //upass/runner:upass_runner_lgraph_test --test_output=errors` -> PASSED
- `bazel test //pass/upass:upass_smoke_suite --test_output=errors` -> PASSED
- `bazel test //pass/upass:upass_lgraph_dry_run_test.sh --test_output=errors` -> PASSED
- `bazel test //pass/upass:upass_smoke_suite --test_output=errors` -> PASSED (post error-path hardening + carry-over test)

## 8) Current Next Steps
1. Extend safe semantic fold coverage for subtraction-equivalent patterns after confirming how minus lowers in this LGraph path.
2. Expand negative-path coverage for additional runtime error scenarios beyond pass-order configuration.
3. Decide whether `inherit:false` should reset additional options beyond `dry_run` and lock that policy in tests.

## 9) Latest Update (Error Hardening + Metadata Preservation)

### Runtime error hardening for `pass.upass`
- Replaced abort-trap style failures with clean runtime errors for:
  - invalid `ir` value
  - all passes disabled
  - missing LGraph input when `ir:lgraph`
- Added runner-side guards to prevent execution if configuration is already invalid:
  - `uPass_runner::run` checks `configuration_error`
  - `uPass_runner_lgraph::run` checks `configuration_error`

### LGraph const rewrite metadata preservation
- For semantic folds that create replacement const nodes, preserved output pin width/sign metadata by calling `set_size(node_out)` before reconnecting sinks.
- Applied to:
  - `fold_sum_const`
  - `fold_neutral_const`

## 10) Latest Update (Shared Adapter Node-View API, Step 1/2)

Implemented a minimal cross-IR node API directly on `IR_adapter` so shared passes can inspect and rewrite nodes without IR-specific types.

New API methods in `upass/core/ir_adapter.hpp`:
- `list_nodes()`
- `op_name(node)`
- `inputs(node)`
- `is_const(node)`
- `const_value(node)`
- `replace_with_const(node, value)`

### LNAST adapter implementation
- Implemented in `upass/core/lnast_manager.hpp`.
- Added stable node id encoding/decoding for `Lnast_nid`.
- `replace_with_const` currently rewrites node payload to const but does not prune children yet (intentional short-term behavior to avoid `lhtree` warning-as-error instantiation from `delete_subtree`).

### LGraph adapter implementation
- Implemented in `upass/core/lgraph_manager.hpp`.
- Exposes node ids from graph NIDs.
- Provides input-node discovery through input edges.
- `replace_with_const` supports:
  - in-place const update when node already `Const`
  - full replacement (create const, reconnect sinks, delete old node) for non-const nodes
  - guard against rewriting graph IO nodes.

### Validation
- `bazel test //upass/runner:upass_runner_cycle_test //upass/runner:upass_runner_lgraph_test --test_output=errors` passed after the API addition.

## 11) Latest Update (Step 3: `fold_sum_const` via Shared Adapter)

`fold_sum_const` in the LGraph runner now executes through shared core logic using only `IR_adapter` calls.

### What changed
- Added shared transform helper in `upass/core/upass_shared.hpp`:
  - `run_fold_sum_const_shared(IR_adapter&, tag, dry_run)`
- Updated `Lgraph_pass_fold_sum_const` in `upass/runner/upass_runner_lgraph.cpp` to call that shared helper instead of `Lgraph_manager::fold_sum_const(...)`.
- Kept pass name and diagnostics format stable:
  - `uPass(lgraph) - sum_const_folded:... rewired:... new_consts:... deleted:... dry_run:...`

### Scope and current limitation
- This shared transform currently matches graph-style `sum` nodes with exactly 2 constant inputs.
- Metric fields (`rewired/new_consts/deleted`) are currently normalized to folded-node count in shared mode to preserve log shape; exact edge-level accounting remains available in `Lgraph_manager::fold_sum_const(...)` legacy path.

### Validation
- Unit:
  - `bazel test //upass/runner:upass_runner_lgraph_test //upass/runner:upass_runner_cycle_test --test_output=errors` -> passed
- Shell:
  - `PATH=/opt/homebrew/opt/bison/bin:$PATH bazel test //pass/upass:upass_lgraph_semantic_fold_test.sh --test_output=errors` -> passed

## 12) Latest Update (Step 4: Shared Parity Tests)

Added adapter-parity unit tests for shared fold behavior:
- New test target:
  - `//upass/core:upass_shared_fold_parity_test`
- New file:
  - `upass/core/upass_shared_fold_parity_test.cpp`

Coverage:
- Same shared helper (`run_fold_sum_const_shared`) exercised over:
  - `Lnast_manager`
  - `Lgraph_manager`
- Mutating mode parity:
  - both report `folded_nodes == 1` on equivalent `2 + 3` setup.
- Dry-run parity:
  - both report fold opportunities while leaving original sum/plus node structure unchanged.

Validation:
- `bazel test //upass/core:upass_shared_fold_parity_test --test_output=errors` -> passed
- Regression:
  - `bazel test //upass/runner:upass_runner_lgraph_test //upass/runner:upass_runner_cycle_test --test_output=errors` -> passed

## 13) Latest Update (Shared `fold_sum_const` Exact Metrics)

Upgraded shared fold accounting from normalized counters to adapter-provided exact effects.

### What changed
- Extended `IR_adapter` with replacement-effect metadata:
  - `Replace_effect{rewired_edges, new_const_nodes, deleted_nodes}`
  - `estimate_replace_with_const(node)`
- Implemented estimates:
  - `Lgraph_manager`:
    - non-const replacement reports exact outgoing-edge rewire count plus `new_const_nodes=1`, `deleted_nodes=1`
    - const/io/invalid replacement reports zero effects
  - `Lnast_manager`:
    - current shared replace path is in-place rewrite, so replacement effects report zeros
- Updated shared helper:
  - `run_fold_sum_const_shared(...)` now sums exact effects per folded node.

### Validation
- `bazel test //upass/core:upass_shared_fold_parity_test --test_output=errors` -> passed
- `bazel test //upass/core:upass_shared_fold_parity_test //upass/runner:upass_runner_lgraph_test //upass/runner:upass_runner_cycle_test --test_output=errors` -> passed

### Tests added/updated
- Added:
  - `pass/upass/tests/upass_invalid_ir_test.sh`
- Updated:
  - `pass/upass/tests/upass_lgraph_missing_input_test.sh` (matches runtime-error behavior)
- Extended unit coverage:
  - `upass/runner/upass_runner_lgraph_test.cpp` now asserts that rewritten outputs in fold paths are driven by `Const` nodes with preserved `bits` and `sign/unsign`.

### New fold coverage started (phase-next)
- Added guarded constant division fold in `fold_shift_div_const`:
  - `div(const_a, const_b) -> const_(a/b)` when `const_b != 0` and both sides are integer constants.
- Added new diagnostics bucket:
  - `to_const_other` (for constant results that are not `0` or `1`).
- Updated runner diagnostics string:
  - now prints `to_const_other` for `shiftdiv_simplified`.
- Added regression in `upass_runner_lgraph_test.cpp`:
  - includes `6/2 -> 3` case and checks rewritten output remains width/sign-correct.

### Cross-IR integration regression
- Added shell regression:
  - `pass/upass/tests/upass_cross_ir_pipeline_test.sh`
- Added shell regression:
  - `pass/upass/tests/upass_cross_ir_inherit_false_test.sh`
- Added to smoke suite:
  - `//pass/upass:upass_smoke_suite`
- Coverage:
  - single pipeline with both modes:
    - `pass.upass ir:lnast order:noop`
    - `pass.lnast_tolg`
    - `pass.upass ir:lgraph order:fold_shift_div`
  - checks LNAST and LGraph order logs, `to_const_other` diagnostics, and convergence.
  - validates label carry-over/reset semantics across IR transition:
    - `dry_run:true` set in LNAST stage carries into next LGraph `pass.upass` stage
    - `inherit:false` on subsequent LGraph stage resets carried labels (`dry_run:false`)

### First truly shared pass logic (not just shared routing)
- Added shared helper:
  - `upass/core/upass_shared.hpp` (`run_noop_shared`)
- Hooked one pass name to the same logic body on both IR runners:
  - `noop_shared` for LNAST runner
  - `noop_shared` for LGraph runner
- LNAST manager now implements `IR_adapter::kind()` so shared logic can identify IR uniformly.
- Added runner tests confirming shared pass resolution on both sides.

### First shared analysis pass
- Extended adapter contract with a common node-count API:
  - `IR_adapter::node_count()`
- Extended adapter contract with shared report fields:
  - `IR_adapter::const_count()`
  - `IR_adapter::arithmetic_count()`
- Implemented on both managers:
  - `Lnast_manager::node_count()`
  - `Lgraph_manager::node_count()`
- Implemented on both managers:
  - `Lnast_manager::const_count()/arithmetic_count()`
  - `Lgraph_manager::const_count()/arithmetic_count()`
- Added shared helper:
  - `run_scan_shared(...)` in `upass/core/upass_shared.hpp`
- Shared helper now returns a common structured report:
  - `Shared_scan_report{node_count, const_count, arithmetic_count}`
- Added pass name `scan_shared` on both runners using same shared helper.
- Added regression:
  - `pass/upass/tests/upass_lnast_shared_scan_test.sh`
  - validates LNAST-side execution of shared analysis logs.

### Validation snapshot
- `//upass/runner:upass_runner_lgraph_test` -> PASSED
- `//upass/runner:upass_runner_cycle_test` -> PASSED

### Current blocker (environment)
- `//pass/upass:upass_smoke_suite` currently fails to build in this environment because external Yosys parser generation requires `bison >= 3.6`, while active build tool reports `bison 2.3`.
- Error observed from Bazel external target:
  - `@@+http_archive+at_clifford_yosys2//:verilog_parser_gen`

### Environment fix validated
- Re-ran smoke with explicit modern `bison` in `PATH`:
  - `PATH=/opt/homebrew/opt/bison/bin:$PATH bazel test //pass/upass:upass_smoke_suite --test_output=errors`
- Result: all smoke targets passed (`21/21`).
- Added local helper script:
  - `pass/upass/tests/run_upass_smoke_local.sh`
