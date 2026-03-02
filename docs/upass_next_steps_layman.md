# upass Next Steps Explained (Layman Version)

This note explains what the next work items mean in plain language, why they matter, and what was started.

## 1) Add more dependency metadata and tests

### In plain language
Think of passes like chores:
- You should wash vegetables before cooking.
- You should not plate food before it is cooked.

Dependencies are these "must happen before" rules.

For `upass`, this means:
- Some passes must run before others, or results are wrong/confusing.

### Example
- `assert` depends on `constprop`.
- If you run just `assert`, runner should automatically do `constprop` first.

### Why it matters
- Prevents hidden bad ordering.
- Makes command usage safer (`order:assert` still works correctly).

### Started
- Already in place: `assert -> constprop` dependency metadata.
- Added test for dependency auto-insertion:
  - `//pass/upass:upass_dependency_test.sh`

## 2) Strengthen convergence coverage

### In plain language
Convergence means:
"Run optimization rounds until nothing changes anymore."

If something keeps changing forever, we stop at `max_iters`.

### Example
- Round 1: simplify `a+b` to known value
- Round 2: nothing new changes
- Stop and report converged

### Why it matters
- Prevents wasted runtime.
- Prevents infinite loops.
- Gives confidence that fixed-point behavior is real.

### Started
- Existing convergence test:
  - `//pass/upass:upass_converge_test.sh`
- Added max-iteration behavior test:
  - `//pass/upass:upass_unknown_and_maxiters_test.sh`

## 3) Improve diagnostics

### In plain language
Diagnostics are "breadcrumbs" that explain what runner did.

### Example
Instead of only seeing "done", you see:
- resolved order: `constprop assert`
- iteration 1 changed: `constprop`
- converged at iteration 2

### Why it matters
- Faster debugging.
- Easier trust in pass behavior.

### Started
- Runner now prints:
  - resolved order
  - changed pass names per iteration
  - max-iteration cap hit message (including `max_iters:1`)

## 4) Move beyond LNAST-only (future)

### In plain language
Today this runner is LNAST-focused.
Goal is one pass engine for both:
- LNAST (tree)
- LGraph (graph)

### Example
Same optimization idea:
- On LNAST, node access walks tree children.
- On LGraph, node access follows pins/edges.
Adapter should hide this difference.

### Why it matters
- One optimization idea, less duplicated logic.
- Better long-term maintainability.

### Started
- Not started in this batch (still planned).

## 5) CI-friendly test target set

### In plain language
We want tests that can run automatically in CI without manual setup.

### Example
`bazel test //pass/upass:...` should validate key upass behavior.

### Why it matters
- Catches regressions early.
- Makes refactors safer.

### Started
- Added multiple `sh_test` targets under `pass/upass/BUILD`.

## 6) Optional docs/UX cleanup

### In plain language
Help text and docs should tell users what to expect.

### Example
- `help pass.upass` should make `order`/`max_iters` obvious.
- Docs should explain convergence in one short section.

### Started
- `help pass.upass` already shows `order` and `max_iters`.
- Added this layman explainer and previous implementation/start docs.

## New/Updated Test Targets

- `//pass/upass:upass_converge_test.sh`
- `//pass/upass:upass_dependency_test.sh`
- `//pass/upass:upass_unknown_and_maxiters_test.sh`
- `//pass/upass:upass_iteration_diag_test.sh`
- `//pass/upass:upass_noop_first_iter_test.sh`
- `//pass/upass:upass_smoke_suite` (runs the full upass smoke set)

Run them:
```bash
PATH=/opt/homebrew/opt/bison/bin:$PATH bazel test //pass/upass:upass_converge_test.sh //pass/upass:upass_dependency_test.sh //pass/upass:upass_unknown_and_maxiters_test.sh --test_output=errors
```

Or with one target:
```bash
PATH=/opt/homebrew/opt/bison/bin:$PATH bazel test //pass/upass:upass_smoke_suite --test_output=errors
```
