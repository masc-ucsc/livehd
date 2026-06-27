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

## Status notes

* `tick` is not parsed by the current LiveHD frontend yet (this is the
  infrastructure being built); a stock `lhd compile` reports `tick` as an
  unknown statement. The **design modules** all lower cleanly today and were
  validated against `iverilog` for the cycle-by-cycle behavior the asserts
  check; the `test`-block driving uses the `tick` cycle-loop semantics the
  runner is being built around.
* `fsm_runner` integer-encodes its state with named `comptime const`s instead of
  an `enum`: an enum-typed register used as a branch/`match` selector does not
  lower in LiveHD yet (`upass.tolg` "unresolved reference"). Compare against the
  named constants, never the raw numbers.
