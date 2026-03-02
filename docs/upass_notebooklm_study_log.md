# LiveHD upass: End-to-End Study + Implementation Log (NotebookLM Handoff)

## 1) What this document is
This is a single handoff doc to help you:
- understand LiveHD + upass in plain language,
- see exactly what has been implemented so far,
- review evidence (tests/log outcomes),
- know what is still pending.

Use this doc as the primary source in NotebookLM for a podcast-style explanation.

---

## 2) Plain-English foundation

### 2.1 What is LiveHD?
LiveHD is a hardware compiler framework. You feed in hardware-like source (for example Pyrope/Verilog paths), and LiveHD transforms it through multiple internal representations to eventually produce optimized hardware-level structures.

### 2.2 Two important IRs
- **LNAST (tree)**: high-level, syntax/structure close to source logic.
- **LGraph (graph)**: low-level, hardware/circuit oriented nodes and edges.

Simple mental model:
- LNAST = “structured recipe”
- LGraph = “wiring blueprint”

### 2.3 What is a compiler pass?
A pass is one transformation/analysis step over IR nodes.
Examples:
- constant folding (`2+3 -> 5`)
- dead code elimination
- copy propagation

### 2.4 What is upass?
upass is a pass framework layer in LiveHD to orchestrate optimization passes. The recent work focuses on making pass logic **shared** across LNAST and LGraph via an adapter contract, instead of duplicating logic per IR.

---

## 3) Architecture (current state)

### 3.1 Core idea
- Shared pass logic sits in `upass/core/upass_shared.hpp`.
- IR differences are hidden behind `IR_adapter`.
- Each IR manager (`Lnast_manager`, `Lgraph_manager`) implements that adapter.
- Runners (`uPass_runner`, `uPass_runner_lgraph`) schedule passes, fixed-point loops, and diagnostics.

### 3.2 Current shared adapter surface
`IR_adapter` now supports both analytics and mutation-friendly node view:
- identity and stats: `kind`, `node_count`, `const_count`, `arithmetic_count`, `fold_candidate_count`
- node view: `list_nodes`, `op_name`, `inputs`, `is_const`, `const_value`
- mutation: `replace_with_const`
- exact effect estimate: `estimate_replace_with_const`

### 3.3 Shared passes currently implemented
- `noop_shared`
- `scan_shared`
- `decide_shared`
- shared `fold_sum_const` transform logic (now used by LGraph runner)

---

## 4) Annotated link index (local source of truth)

### 4.1 Shared core
- [`upass/core/ir_adapter.hpp`](/Users/jamalsyed/compPassProj/livehd/upass/core/ir_adapter.hpp)
  - adapter contract used by shared logic.
- [`upass/core/upass_shared.hpp`](/Users/jamalsyed/compPassProj/livehd/upass/core/upass_shared.hpp)
  - shared pass helpers/reports (`noop`, `scan`, `decide`, shared `fold_sum_const`).

### 4.2 IR adapter implementations
- [`upass/core/lnast_manager.hpp`](/Users/jamalsyed/compPassProj/livehd/upass/core/lnast_manager.hpp)
  - LNAST-side adapter implementation.
- [`upass/core/lgraph_manager.hpp`](/Users/jamalsyed/compPassProj/livehd/upass/core/lgraph_manager.hpp)
  - LGraph-side adapter implementation + graph-specific fold utilities.

### 4.3 Runner wiring and scheduling
- [`upass/runner/upass_runner.cpp`](/Users/jamalsyed/compPassProj/livehd/upass/runner/upass_runner.cpp)
  - LNAST runner integration.
- [`upass/runner/upass_runner_lgraph.cpp`](/Users/jamalsyed/compPassProj/livehd/upass/runner/upass_runner_lgraph.cpp)
  - LGraph runner integration and pass registration.
- [`upass/runner/upass_runner_common.hpp`](/Users/jamalsyed/compPassProj/livehd/upass/runner/upass_runner_common.hpp)
  - shared fixed-point runner contract.

### 4.4 Command entrypoint
- [`pass/upass/pass_upass.cpp`](/Users/jamalsyed/compPassProj/livehd/pass/upass/pass_upass.cpp)
  - `pass.upass` command parsing/routing (`ir`, `order`, `max_iters`, `dry_run`, `inherit`).

### 4.5 Tests
- [`upass/runner/upass_runner_lgraph_test.cpp`](/Users/jamalsyed/compPassProj/livehd/upass/runner/upass_runner_lgraph_test.cpp)
  - unit tests for LGraph runner behavior.
- [`upass/runner/upass_runner_cycle_test.cpp`](/Users/jamalsyed/compPassProj/livehd/upass/runner/upass_runner_cycle_test.cpp)
  - dependency/cycle/order unit tests.
- [`upass/core/upass_shared_fold_parity_test.cpp`](/Users/jamalsyed/compPassProj/livehd/upass/core/upass_shared_fold_parity_test.cpp)
  - shared fold parity tests across LNAST and LGraph adapters.
- [`pass/upass/tests/`](/Users/jamalsyed/compPassProj/livehd/pass/upass/tests)
  - shell-level command pipeline tests.

### 4.6 Build wiring
- [`upass/core/BUILD`](/Users/jamalsyed/compPassProj/livehd/upass/core/BUILD)
- [`upass/runner/BUILD`](/Users/jamalsyed/compPassProj/livehd/upass/runner/BUILD)
- [`pass/upass/BUILD`](/Users/jamalsyed/compPassProj/livehd/pass/upass/BUILD)

### 4.7 Historical progress doc
- [`docs/upass_done_summary.md`](/Users/jamalsyed/compPassProj/livehd/docs/upass_done_summary.md)
  - cumulative implementation history.

---

## 5) Step-by-step implementation log so far

## Phase A: Command + runner hardening
Implemented:
- `pass.upass` options (`order`, `max_iters`, `ir`, `dry_run`, `inherit`) handling and safer error paths.
- dependency-aware pass order resolution.
- fixed-point iteration loop with convergence and max-iteration diagnostics.
- cycle/missing-dependency detection.

Why it matters:
- stable scheduling and predictable execution are required before adding shared semantics.

## Phase B: LGraph path kickoff and guarded semantic folds
Implemented LGraph-side passes and diagnostics:
- `visit`, `fold_scan`, `fold_tag`
- semantic folds: `fold_sum_const`, `fold_neutral`, `fold_shift_div`
- dry-run behavior for semantic folds.

Why it matters:
- gave a concrete low-level optimization playground and test baseline.

## Phase C: Shared scaffolding across IRs
Implemented shared helpers:
- `noop_shared`
- `scan_shared`
- `decide_shared`

Added common report structures and cross-IR pass registration.

Why it matters:
- first proof that one pass name/logic can run on both IR managers.

## Phase D: Adapter contract expansion (shared node view)
Added to `IR_adapter`:
- `list_nodes`, `op_name`, `inputs`, `is_const`, `const_value`, `replace_with_const`

Implemented in both managers.

Why it matters:
- this is the minimum contract required to write real shared transforms.

## Phase E: Shared transform migration (`fold_sum_const`)
Implemented shared transform helper in `upass_shared.hpp` and routed LGraph `fold_sum_const` pass to that helper.

Why it matters:
- first real transform logic moved from IR-specific code path to shared adapter-driven code path.

## Phase F: Parity tests
Added `//upass/core:upass_shared_fold_parity_test`:
- same shared helper tested on LNAST and LGraph.
- mutating and dry-run parity checks.

Why it matters:
- locks behavior and proves shared helper works across both adapters.

## Phase G: Exact shared metrics accounting
Upgraded shared fold metrics from normalized counts to adapter-estimated effects via:
- `IR_adapter::Replace_effect`
- `IR_adapter::estimate_replace_with_const`

Why it matters:
- shared logs are now structurally meaningful (rewires/new consts/deletes), not placeholders.

---

## 6) Test log summary (key evidence)

### 6.1 Core/runner tests
- `bazel test //upass/runner:upass_runner_cycle_test //upass/runner:upass_runner_lgraph_test --test_output=errors`
  - status: **PASS** (latest runs).

- `bazel test //upass/core:upass_shared_fold_parity_test --test_output=errors`
  - status: **PASS**.

### 6.2 Shell integration example
- `PATH=/opt/homebrew/opt/bison/bin:$PATH bazel test //pass/upass:upass_lgraph_semantic_fold_test.sh --test_output=errors`
  - status: **PASS**.

### 6.3 Environment caveat observed
- running larger shell suites can fail if Bazel invokes older bison in environment (`bison 2.3`) for external Yosys parser generation.
- workaround used successfully: prepend modern bison in `PATH`.

---

## 7) Current behavior and known limitations

### 7.1 What is already true
- shared pass infrastructure is real (not just name routing).
- one non-trivial transform (`fold_sum_const`) runs through shared adapter-based logic.
- both IR managers implement the shared node-view contract.
- parity tests verify shared helper behavior in mutating and dry-run modes.

### 7.2 Known limitations
- LNAST `replace_with_const` currently rewrites node payload in place; it does not yet prune/restructure children in shared path.
- shared `fold_sum_const` currently models only 2-input sum-style fold patterns.
- broader transform families (`neutral`, `shift/div`, etc.) are still mostly LGraph-specific implementations.

---

## 8) Pending work (prioritized)

### P0 (next)
1. **LNAST mutation semantics hardening**
- make `replace_with_const` structurally canonical (safe child cleanup / shape invariants).
- add post-mutation invariants checks.

2. **Shared transform expansion**
- migrate next transform to shared logic (recommended: neutral-constant simplifications with clearly safe subset).
- retain per-IR semantics parity tests.

### P1
3. **Metric precision parity**
- ensure shared couxnters are equally informative on LNAST as they are on LGraph.

4. **Cross-IR equivalence fixtures**
- add small canonical fixtures where LNAST and lowered LGraph represent same intent; assert equivalent fold outcomes.

### P2
5. **Documentation cleanup**
- consolidate historical summary into one normalized changelog to avoid duplicate chronology blocks.

---

## 9) Suggested NotebookLM prompts (copy/paste)

1. "Explain this as a beginner: what problem upass solves in LiveHD and why tree-vs-graph matters."
2. "Walk through the implementation timeline and call out why each phase was necessary before the next."
3. "Explain the shared adapter contract in simple terms with a daily-life analogy."
4. "Use the test log section to narrate confidence level, risks, and what is still uncertain."
5. "Generate a 15-minute podcast outline: intro, architecture, implementation story, limitations, and next steps."

---

## 10) Optional external references
- LiveHD docs: [https://masc-ucsc.github.io/docs/livehd/](https://masc-ucsc.github.io/docs/livehd/)
- LiveHD repository: [https://github.com/livehd/livehd](https://github.com/livehd/livehd)

