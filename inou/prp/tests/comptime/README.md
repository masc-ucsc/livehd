# Compile-time tests

Every `:type: comptime` case runs twice through the shared `prplib.py` harness:
first from its original source, then from a scratch copy produced by
`lhd pyrope fmt`. Both runs use the same pass settings and
`:verifier_pass:` / `:verifier_fail:` counts, including expected false assertions.
A failure in either run fails the test. Source files are never formatted in place.

The scratch copy retains sibling imports and resources. Formatting must produce
an output file successfully; that output is compiled and its assertions checked,
so a formatter exit code alone cannot establish semantic preservation.

`format_bool_argument.prp` checks all eight inputs of a boolean expression inside
a named argument. The same before/after check applies to every comptime fixture,
without a formatter-specific runner or a second copy of the test.

```sh
bazel test -c opt //inou/prp:prp-format_bool_argument --test_output=all
python3 inou/prp/tests/pyrope_test.py -i inou/prp/tests/comptime/trivial_if.prp
```
