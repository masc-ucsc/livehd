# upass Phase 4: LGraph Adapter Kickoff

## Goal
Prepare `upass` to support both IRs through a shared execution model:
- LNAST (current)
- LGraph (new path)

## Why this matters
- Today `upass` runner is LNAST-centric.
- Many optimization concepts should be reusable across IRs.
- Adapter boundaries let us share orchestration while isolating traversal/mutation details.

## Proposed Minimal Architecture

### 1) `IR_adapter` (common contract)
Responsibilities:
- expose IR kind (`lnast`/`lgraph`)
- abstract node stepping/traversal hooks
- provide stable interface for runner-level decisions

Initial shape (scaffold):
- `kind()`
- future: `reset_cursor`, `step`, `current_node_kind`, mutation hooks

### 2) `Lnast_manager` (existing)
- remains current backing for LNAST path.
- can later implement/compose `IR_adapter`.

### 3) `Lgraph_manager` (new scaffold)
Responsibilities:
- hold `Lgraph*`
- expose graph-level metadata (`name`, kind)
- future: provide iteration semantics analogous to node-at-a-time walk

## Phase 4 Milestones

1. Add compile-safe scaffolding headers:
- `upass/core/ir_adapter.hpp`
- `upass/core/lgraph_manager.hpp`

2. Keep current behavior unchanged:
- `pass.upass` still drives existing LNAST runner only.

3. Add a design validation checkpoint:
- document how `uPass_runner` may be split into IR-agnostic scheduler + IR-specific walkers.

4. Next implementation target (after this kickoff):
- introduce `uPass_runner_lgraph` prototype that traverses `Lgraph::fast()` and logs visited node types without mutation.

## Implemented in this phase
- Added `upass/runner/upass_runner_lgraph.hpp/.cpp`
  - read-only traversal over `Lgraph::fast()`
  - `visit_fast()` logs visited node type names
  - `collect_type_names()` returns visited type names for tests and future diagnostics
- Added unit test `upass/runner/upass_runner_lgraph_test.cpp`
  - builds a small LGraph with three nodes
  - verifies traversal and collected node type names
- Added Bazel target:
  - `//upass/runner:upass_runner_lgraph_test`

Validation command:
- `bazel test //upass/runner:upass_runner_lgraph_test --test_output=errors`

## Non-Goals in this kickoff
- No runtime switch to LGraph yet.
- No pass behavior changes.
- No modification to existing LNAST flow/tests.
