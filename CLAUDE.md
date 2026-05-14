
See the agent-related details in `AGENTS.md`.

## Contracts

### Compiler warning options

Unless the user explicitly indicates otherwise, do **not** change compiler
warning options to make warnings or errors go away. Always fix the source
code instead.

This includes (non-exhaustive):

- Adding or modifying `-W*`, `-Wno-*`, `-Werror`, `-pedantic`, or `-w` flags
  in `BUILD`, `BUILD.bazel`, `*.bzl`, or any other build configuration.
- Removing warning flags from `tools/copt_default.bzl` or a target's `copts`.
- Disabling diagnostics via `#pragma GCC diagnostic` / `#pragma clang diagnostic`
  pushes around live code.

The only built-in exception is `MODULE.bazel` (external-dep warning
suppression — see AGENTS.md "Compiler Warnings Policy").

Enforced by `scripts/contracts/diff_no_compile_flags_touched.sh`.
