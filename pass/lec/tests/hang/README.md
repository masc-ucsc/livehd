# `lhd lec` cvc5 freeze reproducers

Three tiny Verilog pairs that make `lhd lec` (default cvc5 solver) **freeze
forever**. All three sides are genuinely *equivalent*; the verdict should be
`PROVEN equivalent`, but cvc5 never returns.

```
LHD=./bazel-bin/lhd/lhd
$LHD lec --ref pass/lec/tests/hang/reassoc_ref.v --impl pass/lec/tests/hang/reassoc_impl.v --top foo
$LHD lec --ref pass/lec/tests/hang/distrib_ref.v --impl pass/lec/tests/hang/distrib_impl.v --top foo
$LHD lec --ref pass/lec/tests/hang/poly_ref.v    --impl pass/lec/tests/hang/poly_impl.v    --top foo
```

| case      | identity checked      | ref            | impl           |
|-----------|-----------------------|----------------|----------------|
| `reassoc` | mul associativity     | `(a*b)*c`      | `a*(b*c)`      |
| `distrib` | mul over add          | `a*(b+c)`      | `a*b + a*c`    |
| `poly`    | 3-way mul commute     | `a*b*c`        | `c*a*b`        |

All three are 16-bit. Notes:

- The **encoder is not the problem** — it finishes in milliseconds. The freeze
  is entirely inside cvc5's `checkSat()`. The same shapes at **8 bits** prove in
  ~2s; a single `a*b == b*a` commute even at 64 bits proves instantly. It is the
  *chain of two multiplies* (a nonlinear, multiplier-heavy miter) that explodes
  the bit-blast.

## Root cause (why it was a *freeze*, not a slow solve)

`pass/lec` read a `lec.timeout` knob (`Lec_options::timeout`,
`pass/lec/pass_lec.cpp`, `lhd/lhd_kernel.cpp`) but **never applied it to the
solver** — there was no `solver.setOption("tlimit"/"tlimit-per", ...)` in
`pass/lec/query.cpp`. So `--set lec.timeout=5` was silently ignored and cvc5 ran
unbounded.

## Fix (landed)

`query.cpp`, right after `solver.setLogic(...)`:

```cpp
if (opts.timeout > 0)
  solver.setOption("tlimit-per", std::to_string((long long)opts.timeout * 1000)); // ms per check
```

A timed-out `checkSat` returns `unknown`, which both `checkSat()` sites already
map to `Verdict::Unknown` — a SOUND degrade (never a false Proven/Refuted). The
**CLI default is now `lec.timeout=120` seconds** (`pass_lec.cpp`,
`lhd_kernel.cpp`), so `lhd lec` degrades to `UNKNOWN` instead of freezing; the
library `prove_equal()` default stays `0` (unbounded) for programmatic callers.
Adjust per run with `--set lec.timeout=<seconds>` (`0` = unbounded). Regression:
`//pass/lec:lec_timeout_test`. (`tlimit-per` bounds *each* `checkSat`; `tlimit`
bounds the whole run.)
```
