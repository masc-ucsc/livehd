# upass Execution Plan (Current Kickoff to Next Milestones)

## Goal
Stabilize `upass` as a reliable, debuggable optimization runner in LiveHD, then extend it toward broader pass/dependency coverage and future IR-unification work.

## Current Baseline (Already Done)
- `pass.upass` supports:
  - `order`
  - `max_iters`
- Runner supports:
  - dependency-aware order resolution
  - iterative execution with convergence/max-iter behavior
  - per-iteration change reporting
- Convergence behavior fixed for `constprop`.
- Initial shell regression tests exist and pass.

## Phase 1: Hardening (Now)

### 1.1 Consolidate smoke tests
Deliverables:
- A single Bazel test suite target for all current upass shell tests.
- One command to run all key behavior checks.

Acceptance:
- `bazel test <suite-target>` passes locally.

### 1.2 Keep docs in sync with runnable commands
Deliverables:
- Update docs with the suite command and what each test verifies.

Acceptance:
- New contributor can run tests without guessing commands.

### 1.3 Keep output/debug clarity
Deliverables:
- Verify logs include:
  - resolved order
  - changed passes per iteration
  - convergence vs max-iteration status

Acceptance:
- Debugging a failing pass order no longer needs stepping through code first.

## Phase 2: Dependency Safety Expansion

### 2.1 Add broader dependency metadata
Deliverables:
- Add dependency declarations for more upasses as needed.
- Keep explicit documentation of dependency graph.

### 2.2 Add failure-path tests
Deliverables:
- Tests for:
  - unknown pass name
  - invalid dependency chain/cycle
  - max-iteration fallback path

Acceptance:
- Dependency failures produce deterministic, readable output.

## Phase 3: Convergence Coverage Expansion

### 3.1 Multi-input convergence tests
Deliverables:
- Add multiple real input programs beyond `sum2.prp`.

### 3.2 Negative and edge scenarios
Deliverables:
- Include at least one expected max-iter case.
- Include one case with no-op behavior from first iteration.

Acceptance:
- Convergence logic is validated across diverse patterns, not one golden case.

## Phase 4: Toward IR-agnostic upass

### 4.1 Define adapter boundary for LNAST vs LGraph
Deliverables:
- Minimal interface contract and prototype for dual-IR operation.

### 4.2 Prototype LGraph integration path
Deliverables:
- Skeleton flow to run upass on graph-side IR with equivalent pass semantics where possible.

Acceptance:
- Clear technical migration path from LNAST-only runner to shared engine.

Status update:
- Phase 4 kickoff scaffolding started:
  - `upass/core/ir_adapter.hpp`
  - `upass/core/lgraph_manager.hpp`
  - `docs/upass_phase4_lgraph_plan.md`
  - `upass/runner/upass_runner_lgraph.hpp/.cpp` (read-only prototype)
  - `upass/runner/upass_runner_lgraph_test.cpp`

Validation:
- `bazel test //upass/runner:upass_runner_cycle_test //upass/runner:upass_runner_lgraph_test --test_output=errors`
- `bazel test //pass/upass:upass_smoke_suite --test_output=errors`
- `bazel test //pass/upass:upass_lgraph_mode_test.sh --test_output=errors`

Phase 4 progress:
- Completed feature-gated IR routing in `pass.upass`:
  - `ir:lnast` (default)
  - `ir:lgraph` (read-only traversal prototype path)
- Completed shared fixed-point/diagnostic runner contract:
  - `upass/runner/upass_runner_common.hpp`
  - integrated in both `uPass_runner` and `uPass_runner_lgraph`

## Immediate Next Actions
1. Add subtraction-equivalent identities only after confirming safe lowering patterns in this LGraph path (`Ntype_op::Sub` is a subgraph-instance op).
2. Expand explicit negative-path tests for other pass.upass runtime failures to ensure the clean-runtime-error behavior stays stable.
3. Monitor and refine `inherit:false` UX now that it is fully stage-local for pass option labels.

Status update:
- Completed optional dry-run mode for LGraph semantic rewrites:
  - `pass.upass` label: `dry_run:true|false`
  - supported passes: `fold_sum_const`, `fold_neutral`, `fold_shift_div`
  - diagnostics include `dry_run:` marker in per-pass summaries
- Added dry-run tests:
  - `//pass/upass:upass_lgraph_dry_run_test.sh`
  - `UpassRunnerLgraph.FoldShiftDivDryRunNoMutation`
- Completed additional guarded identity rewrites:
  - `div(c,c) -> 1` for known non-zero constants (`fold_shift_div`)
  - `and(x,1) -> x` for proven 1-bit `x` (`fold_neutral`)
  - `or(x,1) -> 1` for proven 1-bit `x` (`fold_neutral`)
- Added/updated regression coverage:
  - `//pass/upass:upass_lgraph_neutral_fold_test.sh`
  - `//pass/upass:upass_lgraph_shift_div_fold_test.sh`
  - `//upass/runner:upass_runner_lgraph_test`
- Completed parser/error-path hardening for invalid pass requests:
  - unknown pass and dependency-cycle requests now emit clean runtime errors and `command aborted...`
  - applies to both `ir:lnast` and `ir:lgraph`
- Added explicit carry-over regression:
  - `//pass/upass:upass_lgraph_label_carryover_test.sh`
- Added inheritance reset control:
  - `pass.upass inherit:false` (default remains `inherit:true`)
  - reset scope: all pass option labels for the stage, with explicit-stage reapply
  - regression: `//pass/upass:upass_lgraph_inherit_false_test.sh`
