# `tests/style/` — style-hint tests

Pins `lhd pyrope style` (analyzer: [`inou/prp/pyrope_style.cpp`](../../pyrope_style.cpp),
CLI: `lhd/lhd_pyrope.cpp`). The counterpart of [`tests/warnings/`](../warnings/) for
source-only style hints: every `*.prp` here auto-generates a `prp-style-<name>`
`bazel test` target (see `inou/prp/BUILD`), driven by `tests/pyrope_test.py` and
`PrpRunner.run_style`. Without these a regression in a rule would pass silently.

## Header

```
/*
:name: <test name>
:type: style
:style: <rule code> [<rule code> ...]     (or `none` for a clean file)
:style_args: <extra CLI args>             optional, e.g. --min-repeats 3
:message: <regex on the finding message(s)>   optional
:help:    <regex on the finding hint(s)>      optional
*/
```

- `lhd pyrope style FILE --diag-fmt json` must report **exactly** the listed rule
  codes (no more, no fewer) and exit 2 (0 for `none`). A partial parse, an
  error, or a warning fails the test: a fixture only has to *parse*, nothing is
  compiled.
- Every hint must name its silencing tag, `// prp-style-allow <code>`.
- `locate_style_<code>` in a comment pins the line where that rule's finding
  starts (same idea as `locate_error_here` / `locate_warning_here`).

## What is covered

One fixture per rule (`repeated-code`, `likely-unrolled-loop`, `whole-tuple-copy`,
`flattened-bundle-arguments`, `single-destination-conditional`, `hardcoded-reset`,
`reset-port-type`, `narrow-scoped-mut`, `compact-bit-packing`), the `match` form of `single-destination-conditional`
(`single_destination_match`, `single_destination_match_clean`), plus the reset negatives (`reset_clean`, `reset_used_as_data`,
`reset_not_a_priority_clear`), the active-low hint, the "all three hints" shape
(`reset_hand_written_u1`, the twin of `tests/equiv/struct_top_port.prp`), and the
`prp-style-allow` scoping rules (`allow_scope`, `allow_file_scope`).

The const/packing rules also have clean near-misses and a complete-array fixture;
the CLI regressions cover def/use escapes, retained bits, widths, ordering, side
effects, silencing and partial parses.

To add a rule: add a positive fixture, a near-miss `none` fixture, and extend
`lhd/tests/lhd_style_test.py` for the detailed corner cases.
