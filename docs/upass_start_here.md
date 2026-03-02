# upass Start Here

This is a practical kickoff guide for working on LiveHD unified passes (`upass`).

## 1) Current Upass Stack in LiveHD

- Entry pass command: `pass.upass`
  - Wrapper source: `pass/upass/pass_upass.cpp`
- Runner (node-at-a-time over LNAST): `upass/runner/upass_runner.cpp`
- Core plugin API: `upass/core/upass_core.hpp`
- Existing upasses:
  - `verifier`: `upass/verifier/`
  - `constprop`: `upass/constprop/`
  - `assert`: `upass/assert/`

## 2) Build and Smoke Run

Prerequisite: `bazel` or `bazelisk` installed locally.

```bash
bazel build //main:lgshell
./bazel-bin/main/lgshell "help pass.upass"
```

Minimal smoke command:
```bash
./bazel-bin/main/lgshell "pass.upass"
```

## 3) Selecting Pass Sequence

`pass.upass` supports two selection styles:

- Boolean toggles:
```text
pass.upass verifier:true constprop:true assert:false
```

- Explicit ordered list (`order` overrides booleans):
```text
pass.upass order:verifier,constprop,verifier
```

Use `order` when experimenting with pass interactions or fixed-point style reruns.

Iteration control:
```text
pass.upass max_iters:3
pass.upass order:constprop,assert max_iters:5
```

IR selection:
```text
pass.upass ir:lnast   # default
pass.upass ir:lgraph  # read-only graph traversal mode
```

LGraph mode example:
```text
inou.pyrope files:inou/pyrope/tests/sum2.prp |> pass.lnast_tolg |> pass.upass ir:lgraph
```

LGraph mode default pass order:
```text
visit,fold_scan
```
You can override with `order:` in the same way as LNAST mode.

Optional mutation pass (guarded):
```text
pass.upass ir:lgraph order:fold_tag max_iters:3
```
`fold_tag` does metadata mutation only (node color tagging) and is useful as the first safe mutation path.

First semantic mutation pass (guarded):
```text
pass.upass ir:lgraph order:fold_sum_const max_iters:3
```
`fold_sum_const` rewrites `sum(const,const)` nodes into const nodes and reconnects outputs.
It also reports rewrite metrics each iteration: `rewired`, `new_consts`, `deleted`.

Neutral-constant simplification pass (guarded):
```text
pass.upass ir:lgraph order:fold_neutral max_iters:3
```
`fold_neutral` applies safe identities like:
- `sum(x, 0) -> x`
- `or(x, 0) -> x`
- `xor(x, 0) -> x`
- `and(x, 0) -> 0`
- `mult(x, 1) -> x`
- `mult(x, 0) -> 0`
- `or(x, x) -> x`
- `xor(x, x) -> 0`
- `and(x, x) -> x`
- `and(x, 1) -> x` (only when `x` is 1-bit wide)
- `or(x, 1) -> 1` (only when `x` is 1-bit wide)
It also reports rewrite metrics each iteration: `rewired`, `new_consts`, `deleted`.

Shift/div simplification pass (guarded):
```text
pass.upass ir:lgraph order:fold_shift_div max_iters:3
```
`fold_shift_div` applies guarded cases:
- `div(x,1) -> x`
- `shl(x,0) -> x`
- `sra(x,0) -> x`
- `div(0,c) -> 0` when `c` is a known non-zero constant
- `div(c,c) -> 1` when `c` is a known non-zero constant
It also reports rewrite metrics each iteration: `rewired`, `new_consts`, `deleted`.

Dry-run mode for semantic LGraph rewrites:
```text
pass.upass ir:lgraph order:fold_sum_const dry_run:true max_iters:1
```
When `dry_run:true`, rewrite passes report what they would rewrite, but do not mutate the graph.
Current dry-run-aware passes:
- `fold_sum_const`
- `fold_neutral`
- `fold_shift_div`

Pipeline inheritance control:
```text
pass.upass ir:lgraph order:fold_shift_div inherit:false max_iters:1
```
`inherit:false` resets inherited `pass.upass` option state for that stage and reapplies only labels explicitly written in that stage.
This applies to: `order`, `max_iters`, `ir`, `dry_run`, `verifier`, `constprop`, `assert`, `inherit`.

Current dependency metadata:
- `assert` depends on `constprop` (runner auto-inserts dependencies before requested pass order).

Invalid pass configuration behavior:
- Unknown pass names and dependency-cycle requests now fail with a clean runtime error and `command aborted...` output (no abort trap).
- This applies to both:
  - `ir:lnast`
  - `ir:lgraph`

## 3.1 Regression Test Suite

Run all upass smoke checks with one target:
```bash
PATH=/opt/homebrew/opt/bison/bin:$PATH bazel test //pass/upass:upass_smoke_suite --test_output=errors
```

Suite coverage:
- convergence path (`upass_converge_test.sh`)
- command-path dependency cycle handling (`upass_cycle_cmd_test.sh`)
- dependency auto-ordering (`upass_dependency_test.sh`)
- per-iteration diagnostics output (`upass_iteration_diag_test.sh`)
- malformed `order` validation error path (`upass_order_parse_test.sh`)
- `max_iters:0` normalization (`upass_maxiters_zero_test.sh`)
- no-op first-iteration convergence (`upass_noop_first_iter_test.sh`)
- LGraph routing + traversal logs (`upass_lgraph_mode_test.sh`)
- LGraph mode missing-input rejection (`upass_lgraph_missing_input_test.sh`)
- LGraph guarded mutation pass and convergence (`upass_lgraph_mutation_test.sh`)
- LGraph command-path dependency cycle (`upass_lgraph_cycle_cmd_test.sh`)
- LGraph command-path unknown pass handling (`upass_lgraph_unknown_test.sh`)
- LGraph semantic fold mutation (`upass_lgraph_semantic_fold_test.sh`)
- LGraph neutral-constant simplification (`upass_lgraph_neutral_fold_test.sh`)
- LGraph shift/div simplification (`upass_lgraph_shift_div_fold_test.sh`)
- LGraph semantic-fold dry-run behavior (`upass_lgraph_dry_run_test.sh`)
- LGraph inheritance reset behavior (`upass_lgraph_inherit_false_test.sh`)
- LGraph label carry-over behavior across pipeline stages (`upass_lgraph_label_carryover_test.sh`)
- unknown pass + max-iteration behavior (`upass_unknown_and_maxiters_test.sh`)

## 4) First Implementation Targets

1. Add dependency metadata in upass plugins (or a small manager) to avoid hardcoding order.
2. Add focused tests for:
   - unknown pass handling,
   - ordering behavior,
   - cycle behavior and repeated pass execution semantics.
3. Start `LGraph` integration by introducing an adapter boundary in `upass/core`.

## 5) Suggested Near-Term File Changes

- Dependency/order manager:
  - `upass/core/upass_core.hpp`
  - `upass/runner/upass_runner.hpp`
  - `upass/runner/upass_runner.cpp`
- Pass wrapper labels/UX:
  - `pass/upass/pass_upass.cpp`
- Build/test targets:
  - `upass/runner/BUILD`
  - `pass/upass/BUILD`
