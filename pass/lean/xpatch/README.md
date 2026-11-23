# xpatch: state transport between two versions of a design

Given a simulated design A and a changed version B, xpatch computes B's state at a cycle from
A's saved states and continues B's simulation from there, with no cycle of B simulated to get it.
`run.sh` saves A's state at every cycle (a monitor may read it; this example's monitor reads only
the inputs). The jump is justified by a relation proved with `sym_verify` on the two designs'
product, and Lean checks the emitted theorem.

## Relation to existing tools

- `sym_refmap` already proves that an implementation refines a spec through a refinement map,
  with stuttering, and is more general than xpatch on timing.
- The `lhdcert-ckpt` checkpoints, `cert_trace` and `sym_verify` already exist, and each works on
  one design at a time.
- `sym_verify` can also show that a checkpoint can't occur, a cheap way to reject it before
  simulating.
- xpatch adds the jump between two designs: it builds B's full state from A's saved state and a
  monitor (a little extra state computed along A's run), then resumes B from it. With B as the
  implementation and A as the spec, `sym_refmap`'s map goes the other way (it reads the spec's
  state off the implementation); it needs a memory-free spec (both designs here have a memory);
  and its base case starts from the all-zero state, where xpatch starts from power-on.

## How it works

For a pair of designs, write:

- a **monitor** `h`, updated each cycle from A's state and input: `h' = mon h s i`;
- a **relation** `R s h` that builds a B state from an A state and a monitor value;
- a start value `h0` with `R (A's power-on state) h0 = B's power-on state`.

The one obligation is a commuting square, for every state, monitor value and input:

    stepB (R s h) i  =  R (stepA s i) (mon h s i)

Then B's state N cycles after power-on is `R s_N h_N`, with `s_N` and `h_N` taken from A's run.
`Transport.lean` proves this for any step functions. For a concrete pair, the base fact and the
square are proved as an invariant of a **product design** (A and B side by side, plus the monitor,
with an output `ok` that is 1 when B's state equals `R (A's state) h`) by
`sym_verify ... ok k:1 init:reset`, and its `emit:` theorem is compiled by Lean.

## Example

- **A**: an 8-bit accumulator, a free-running counter and a 16-word memory (the design of
  `pass/lean/tools/tests/lhd_ckpt_import_test.sh` with 16 words instead of 12, so every 4-bit
  address is inside the memory).
- **B**: A with one more register, `flg`, a flag set when a write stores 0xFF and never cleared.
  No output reads `flg`, so B's outputs equal A's, and the checks compare full states.
- **Monitor and R**: A's state does not hold the flag (a word written with 0xFF can be
  overwritten later), so the monitor keeps the same flag along A's run; R copies A's state and
  sets `flg` to the monitor's flag.
- **Control**: a monitor whose flag clears on a write of any other value, which must fail.

## Run

With `bazel-bin/lhd/lhd` (`bazel build //lhd:lhd`) and `formal/lean/.olean-dev`
(`formal/lean/.build-proj.sh`) built, and `lean` and `cadical` from the toolchain that
`formal/lean/lean-toolchain` names on `PATH`:

```bash
bash pass/lean/xpatch/run.sh      # LHD=<lhd>, OUT=<dir> override defaults; output in generated/xpatch
```

It ends with `xpatch: PASS (<n> of <n> checks passed)`. It checks that the proof goes through and
that Lean accepts the theorem and refuses three corrupted copies of it; that the jumped states at
cycles 10, 20, 30 and 40 equal B's states in a run of B from power-on; that B continues correctly
from a jumped state; and that the control fails. Every command and its output are in
`generated/xpatch/run.log`. `run.sh` uses GNU tools (`sha256sum`, `timeout`) and was run on Linux.

## Files

| file | contents |
|---|---|
| `run.sh` | the whole flow and its checks |
| `gen.py` | writes A, B, the product designs and the testbench |
| `transport.py` | applies the monitor and R to A's run |
| `checks.py` | comparisons and their selftest |
| `Transport.lean` | the general theorem |

## Current limits

- R and the monitor are hand-written, and written twice (Pyrope for the proof, Python for the
  jump). Nothing proves the two are the same function.
- `Transport.lean` is not linked to LiveHD's semantics, and the emitted theorem relies on
  `native_decide`.
- One toy pair on a 40-cycle testbench. Nothing measures speed.
