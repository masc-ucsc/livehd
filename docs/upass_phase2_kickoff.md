# upass Phase 2 Kickoff

This document tracks the next execution block after the initial upass hardening.

## Objective
Expand safety/observability coverage so dependency and iteration behavior stays stable under future refactors.

## Scope for this kickoff
- Add documentation status tracking for Phase 2 tasks.
- Add at least one new regression test for runner diagnostics.
- Keep `upass_smoke_suite` as the single command entrypoint.

## Task Checklist
- [x] Phase 2 kickoff doc created.
- [x] Add cycle/error-path coverage test.
- [x] Add iteration diagnostics regression test.
- [x] Add first-iteration no-op convergence coverage.
- [x] Wire new test into `upass_smoke_suite`.
- [x] Re-run suite and verify all tests pass.

## Notes
- Existing tests already cover:
  - convergence behavior
  - dependency auto-insertion (`assert -> constprop`)
  - unknown pass and max-iteration fallback
- This kickoff adds explicit coverage for:
  - per-iteration diagnostics output
  - dependency cycle/missing-dependency resolution failure paths
  - no-op pass convergence at iteration 1

## Run Command
```bash
PATH=/opt/homebrew/opt/bison/bin:$PATH bazel test //pass/upass:upass_smoke_suite --test_output=errors
```
