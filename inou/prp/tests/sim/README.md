# Pyrope simulation tests (`:type: simulation`)

Example designs that drive a DUT from a Pyrope `test` block and check the
result with an `assert` **at the end of simulation**. These exercise the
`simulation` mode of the test runner (`prplib.py`, the `run()` dispatch).

## Pattern

Each file pairs a synthesizable design with one or more `test` blocks:

* The DUT is **called inside** the cycle loop, once per cycle
  (`const v = dut(in=val)`). The call drives this cycle's inputs and returns
  this cycle's output. A one-time call made *before* the loop does **not**
  re-evaluate, so the call belongs in the loop body.
* `tick N { ... }` runs `N` cycles, advancing **one clock per iteration**.
  Do **not** put a `step` inside a `tick` -- that would advance a second clock.
  `tick { ... break }` runs until a runtime condition (with `N` as a watchdog).
* The per-cycle output is captured into an outer `mut` (declared before the
  loop); the end-of-sim `assert` checks that captured value. Test-local `mut`s
  persist across cycles.
* A test-local `mut` golden value, updated in lockstep inside the same loop,
  mirrors the design's next-state so the final `assert` is self-checking.

`poke` / `peek` / `sigref` / `regref` are intentionally **not** used here yet.

## Examples

| File | Design | Tests | Constructs |
|------|--------|-------|------------|
| `counter.prp`     | up-counter             | `counter.held_high`, `counter.gated`    | `tick N`, lockstep golden |
| `accumulator.prp` | running-sum            | `accum.sum`                             | `tick N`, changing input |
| `fsm_runner.prp`  | Idle→Run→Done FSM      | `runner.until_done`, `runner.watchdog`  | `tick { break }`, `tick N` watchdog |
| `seq_detect.prp`  | "11" sequence detector | `detect.stream`                         | `tick N`, streamed pattern, golden |
| `test_args.prp`   | adder                  | `adder.params`                          | `test name(params)` + `--arg`, default/required/override |
| `tick_comptime_survives.prp` | passthru    | 2 blocks                                | what stays **comptime** across a `tick` (see below) |
| `tick_comptime_opaque.prp`   | up-counter  | 7 blocks                                | what a `tick` must make **opaque** (see below) |

## `tick` and constant propagation

`tick` is a loop that is deliberately **not unrolled**, so constprop cannot
reason about its body the way it does about straight-line code. Three rules
govern what may be folded across one (user ruling 2026-07-25):

* **R0** — the *iteration count* is assumed **unknown**, always, including when
  it is written as a literal. A tick count is routinely overridden by a runtime
  argument, so never optimize on it.
* **R1** — a variable updated inside a tick body with anything other than a
  constant literal (`a = 3`) is an *induction variable*: assume not-constant,
  even when it happens to be constant.
* **R2** — R0 collapses the rest. Since the body may run zero times, a variable
  written anywhere inside a tick is `prior-value`-or-`written-value` afterwards,
  which is not knowable. So **any** write inside a tick makes that variable
  opaque.

What survives is therefore exactly one thing: **a variable the tick never
writes** (and, correspondingly, a `comptime const` merely *read* inside the
body — a read is not an update).

The pair `tick_comptime_survives.prp` / `tick_comptime_opaque.prp` is one
specification split by expected verdict. The first uses `cassert`, which
hard-errors on a *wrong* fold and degrades to a runtime check when merely
unproven — correct, since failing to fold there is conservative rather than
wrong. The second uses runtime `assert` only: a wrong fold shows up as
`error[cassert-false]`, because a variable folded across a tick almost always
folds back to its declared initializer.

Three fixtures in `opaque` bound R0 and should be read as a set — they are the
same lone, unconditional literal write under three different counts:

| fixture | count | correct value |
|---|---|---|
| `lone_literal_write_still_opaque` | literal `4` | 7 |
| `zero_trip_keeps_outer_value`     | literal `0` | 5 |
| `runtime_trip_count`              | `--arg cycles=N` | 7 for N>0, **5 for N=0** |

The last one is the argument for R0 in one line: identical source text yields
`rt_trip=7` by default and `rt_trip=5` under `--arg cycles=0`. Do not
reintroduce a "literal count >= 1 is foldable" special case — the middle row is
the counterexample.

Both files are **live gates**. `tick`/`step` lower into LNAST (a `tick` node,
dispatched from `Prp2lnast::stmt_dispatch`) and `uPass_runner::tick_uncertain_body`
walks the body in an uncertain scope, registering every write so it is
invalidated on loop exit.

One consequence worth knowing when writing new fixtures: a tick body is emitted
**verbatim**, not folded. Only its writes are registered. That is sufficient for
the rules above (nothing inside a tick may be folded anyway) and it sidesteps a
gap — upass has no model of a DUT *instance*, so `mut acc = counter` binds none
of the module's ports and folding `got = acc.v` would raise a spurious
"unknown field `v` on tuple `acc`". The practical limit: a `cassert` placed
*inside* a tick body is not evaluated at compile time. Put comptime assertions
after the loop, as both fixtures do.

Before this landed, `tick_statement` had no dispatch entry at all: the whole body
was silently dropped on the way to LNAST while `prp_sim.cpp`'s independent CST
walk still executed it, so a testbench `assert` folded against the stale
initializer of the variable it captured (lhdsuite fixme issue 2).

## Runtime parameters (`test name(params)` + `--arg`)

A `test` may declare runtime parameters — `test add.checked(base:u32=10, gain:u32)`
— that drive the DUT, size a `tick` loop, or seed a model (see
[Testing](../../../../../docs/docs/pyrope/05b-statements.md#testing-test)). A
parameter with a default is optional; one with no default (or `=nil`) is
required. Bind them on the command line with `--arg name=value` (repeatable),
which wins over the default; a required parameter left unset is an error, never
a silent `0`:

```bash
lhd sim test_args.prp adder.params --arg gain=3 --arg count=4
```

In a `:type: simulation` test the bindings come from the `:args: k=v k=v` header
tag (the runner forwards each as `--arg k=v`).

## Running

These run as part of `bazel test //inou/prp:all` (targets `prp-sim-<name>`).
The harness (`prplib.py`, `:type: simulation`) drives `lhd sim --setup-only` to
lower each DUT to header-only `Slop<N>` C++ and emit one driver per `test`
block, then compiles each driver with the host C++ compiler and runs it — a
non-zero exit means an `assert` fired. Because `Slop`/`Blop` are header-only and
`-DNDEBUG` drops the `iassert` checks, a driver has **no link dependencies** (no
nested bazel, no abseil, no network); only hlop's and iassert's headers are
staged into the test runfiles (the `sim_runtime_hdrs` data dep in `BUILD`).

To run one directly: `bazel test //inou/prp:prp-sim-counter --test_output=all`.

## Status notes

* `fsm_runner` integer-encodes its state with named `comptime const`s instead of
  an `enum`: an enum-typed register used as a branch/`match` selector does not
  lower in LiveHD yet (`upass.tolg` "unresolved reference"). Compare against the
  named constants, never the raw numbers.
