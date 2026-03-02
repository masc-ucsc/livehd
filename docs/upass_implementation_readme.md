# upass Implementation README (Work Completed)

## Scope
This document records what was implemented to start and harden `upass` in LiveHD, including build/tooling setup, code changes, and test validation.

## Environment Setup Completed
- Cloned upstream LiveHD into:
  - `/Users/jamalsyed/compPassProj/livehd`
- Installed build tools:
  - `bazelisk` (Bazel launcher)
  - `bazel` runtime resolved to `9.0.0`
  - `bison 3.8.2` (required by Yosys parser generation in build)

Build command used:
```bash
cd /Users/jamalsyed/compPassProj/livehd
PATH=/opt/homebrew/opt/bison/bin:$PATH bazel build //main:lgshell
```

## Functional Changes Implemented

### 1) `pass.upass` command controls
Added new optional labels:
- `order`: explicit comma-separated upass execution list
- `max_iters`: max iteration count for fixed-point style runs

Behavior:
- If `order` is provided, it overrides boolean toggles (`verifier`, `constprop`, `assert`).
- `max_iters` defaults to `1` and clamps invalid values (`0` -> `1`).

Files:
- `pass/upass/pass_upass.cpp`
- `pass/upass/pass_upass.hpp`

### 2) Dependency-aware ordering in runner
`uPass_runner` now resolves pass order with dependency handling:
- Plugin metadata supports dependency lists.
- Runner performs DFS ordering.
- Detects and reports invalid dependency chains/cycles.

Files:
- `upass/core/upass_core.hpp`
- `upass/runner/upass_runner.hpp`
- `upass/runner/upass_runner.cpp`

### 3) Iterative execution + convergence guard
`uPass_runner::run(max_iters)` now:
- Runs multiple iterations up to `max_iters`
- Resets per-pass changed flags each iteration
- Stops early on convergence (`no pass changed`)
- Reports:
  - `uPass - converged at iteration N`
  - or `uPass - reached max iterations (N)`

Files:
- `upass/core/upass_core.hpp`
- `upass/runner/upass_runner.hpp`
- `upass/runner/upass_runner.cpp`

### 4) Dependency metadata for built-in pass
Declared:
- `assert` depends on `constprop`

Result:
- `order:assert` automatically schedules `constprop` first.

File:
- `upass/assert/upass_assert.hpp`

### 5) Real change tracking in constprop
Initial issue:
- `Symbol_table::set(...)` always returns `true`, causing false change signals and preventing convergence.

Fix:
- `uPass_constprop` now compares previous and new values before calling `mark_changed()`.
- For trivials: compares `st.get_trivial(var)` vs new `Lconst`.
- For refs/bundles: compares current bundle pointer vs RHS bundle pointer.

File:
- `upass/constprop/upass_constprop.cpp`

### 6) Duplicate plugin registration warning removed
Removed redundant plugin header includes in `pass_upass.cpp` that caused:
- `uPass_plugin: constprop is already registered`

File:
- `pass/upass/pass_upass.cpp`

## Documentation Added
- Starter usage/design doc:
  - `docs/upass_start_here.md`

This file includes:
- Current upass stack locations
- How to build and run
- How to use `order` and `max_iters`
- Near-term next implementation targets

## Regression Test Added
Added Bazel shell test to validate convergence behavior on real input:
- Target: `//pass/upass:upass_converge_test.sh`
- Script:
  - `pass/upass/tests/upass_converge_test.sh`

What it checks:
- Runs:
  - `inou.pyrope files:.../sum2.prp |> pass.upass order:constprop,assert max_iters:5`
- Asserts output contains:
  - `uPass - converged at iteration 2`
- Asserts output does **not** contain:
  - `uPass - reached max iterations`

BUILD wiring:
- `pass/upass/BUILD`

## Validation Performed

### Build
```bash
PATH=/opt/homebrew/opt/bison/bin:$PATH bazel build //main:lgshell
```
- Status: successful

### Command-level smoke checks
Interactive pipeline run (with `HOME=/tmp` for sandbox-safe history path):
```bash
printf 'help pass.upass\nquit\n' | HOME=/tmp ./bazel-bin/main/lgshell
printf 'inou.pyrope files:inou/pyrope/tests/sum2.prp |> pass.upass order:assert max_iters:2\nquit\n' | HOME=/tmp ./bazel-bin/main/lgshell
```

Confirmed:
- `help pass.upass` shows `order` + `max_iters`
- `order:assert` auto-adds `constprop`
- convergence message appears after constprop fix

### Test run
```bash
PATH=/opt/homebrew/opt/bison/bin:$PATH bazel test //pass/upass:upass_converge_test.sh --test_output=errors
```
- Status: PASSED

## Known Limitations / Next Steps
1. Dependency metadata currently exists only for `assert -> constprop`.
2. No generalized pass dependency declaration or cycle tests beyond current runner behavior.
3. Convergence correctness is validated for one input (`sum2.prp`); add more cases.
4. This work is still LNAST-focused; LGraph-side unification via adapters is future work.

