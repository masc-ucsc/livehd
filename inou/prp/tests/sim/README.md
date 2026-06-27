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
