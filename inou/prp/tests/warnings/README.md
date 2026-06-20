# `tests/warnings/` — expected-warning tests

The lint counterpart of [`tests/errors/`](../errors/). Each `*.prp` here must
**compile cleanly** (exit 0, no `error` diagnostic) yet emit at least one
**warning** diagnostic. The header pins what the warning must say.

Every `tests/warnings/*.prp` auto-generates a `prp-warn-<name>` `bazel test`
target (see `inou/prp/BUILD`).

## Header

```
/*
:name: <test name>
:type: warning
:warning: <regex matched against the emitted warning message(s)>
:help:    <optional regex matched against the warning hint(s)>
*/
```

- `:type: warning` selects the warning harness (`PrpRunner.run_warning`).
- `:warning:` and `:help:` are matched with `re.search` (literal-substring
  fallback when the value is not a valid regex), exactly like `tests/errors/`'s
  `:error:` / `:help:`.
- The diagnostics are read from the JSONL sink declared via
  `lhd --emit diagnostics:PATH`; only records with `severity == "warning"` are
  considered, and any `severity == "error"` record (or a non-zero exit) fails
  the test — a warning test asserts the code is merely lint-worthy, not broken.

## Pinning the line

Put a `locate_warning_here` comment on the line where the warning is expected.
The harness checks a warning's `span.start_line` matches it. Using a marker
(instead of a hard-coded number) keeps the test correct when lines are
added/removed above it. This mirrors `locate_error_here` in `tests/errors/`.

## What is currently checked

`unused-expression` (a pure expression used as a statement — `a + 1`, a bare
`a`, `a == b`, a non-final expression in a code block — whose computed value is
discarded). The front-end (`inou/prp/prp2lnast.cpp`) skips the warning when the
expression has an observable side effect (a function call, an assignment, an
`::[attr=…]` write, an embedded scope / if / match / loop).
