# Differential simulation equivalence (`prp-simeq-*`)

Every equiv pair `<name>.prp` (DUT) + `<name>.v` (golden Verilog) gets two
generated testbenches that drive **the same** constrained-random stimulus and
compare an output **signature** (hash):

| file | driver | role |
|------|--------|------|
| `<name>_tb.v`   | Icarus Verilog (`iverilog`/`vvp`) | golden — produces the reference signature |
| `<name>_tb.prp` | `lhd sim` (→ `inou.cgen.sim` C++)  | under test — recomputes the signature and `assert`s it equals the golden |

This checks the **`lhd sim` C++ code generator** (`inou/cgen/cgen_sim.cpp`)
against an independent oracle, complementing the *formal* `prp-equiv-*` (LEC)
and `prp-v2prp-*` checks. A miscompile that both LEC and the Verilog cgen happen
to share can still be caught here, because the reference is Icarus, not LiveHD.

## How stimulus/hash parity is guaranteed

Both testbenches run a bit-identical 64-bit **LCG** (`s = s*A + C`, mod 2^64) to
make each cycle's inputs and a bit-identical **rolling hash**
(`sig = sig*P + out`) to fold the outputs. Only `+ * &` are used, so the math
matches between Verilog `reg [63:0]` (unsigned) and the generated driver's `long`
(two's-complement) exactly — no `>>` (arithmetic-vs-logical differs). Each input
field gets its own LCG draw masked to its width; each output is folded **masked
to its width** (so a signed output hashes the same on both sides); the exposed
signature is masked to 62 bits so it is a clean non-negative decimal.

Cycle alignment mirrors the instance/`step` model: drive inputs → `step`
(clock edge) → peek/sample outputs. Reset is a poked input (`acc.reset =
clock < 4`). A sequential DUT skips the reset window in the hash; a *reset-less*
sequential DUT (a pure-feedforward pipe) instead skips a 32-cycle fill window so
the power-on state (`X` in Verilog, the init in the driver) flushes out before
hashing begins. Combinational DUTs hash every cycle. 1000 cycles total.

The golden Verilog TB samples **after the full clock period settles** (both
the rising and the falling half), never right after the rising edge alone: one
`step`/peek in the `lhd sim` model observes the flop's *settled* value for the
whole cycle regardless of its `posclk` (posedge/negedge) attribute, since
`posclk` only picks which sub-tick a VCD dump lands in — it is otherwise a
no-op in the cycle-based sim scheduler (see `inou/cgen/cgen_sim.cpp`'s `Flop`
comment). A negedge-clocked flop only updates on the falling half, so sampling
right after the rising edge alone would read its *stale*, previous-cycle
value against the golden while `lhd sim` reports the current cycle's value —
a spurious mismatch, not a real `lhd sim` bug. Posedge-only DUTs are
unaffected by where exactly post-edge sampling happens (no other input
changes between the two edges within one generated cycle), so this is a safe
default for every pair, not just the negedge ones.

## Two phases

* **SETUP** (only when a pair is *added* or its logic/interface changes; needs
  `iverilog` on `$PATH`):

  ```
  python3 inou/prp/tests/gen_equiv_tb.py --setup            # all pairs
  python3 inou/prp/tests/gen_equiv_tb.py --setup --name foo # one pair
  ```

  This (re)emits both `_tb` files, runs the Verilog golden, bakes the resulting
  signature into `<name>_tb.prp`'s `assert`, and marks the DUT lambda `pub`
  (`inou/prp/tests/pubify_equiv.py`) so the `_tb.prp` can `import` it.

* **RUN** (the regression, part of `bazel test //...`): each `prp-simeq-<name>`
  target runs `lhd sim <name>_tb.prp`, host-compiles the driver hermetically
  (same path as `prp-sim-*` — the `sim_runtime_hdrs` headers, no nested bazel),
  and checks the baked golden. No Verilog needed at run time.

Run one: `bazel test //inou/prp:prp-simeq-bitset_reg --test_output=all`.

## Coverage / gates

`gen_equiv_tb.py` cannot generate a testbench for a pair whose golden Verilog
has a non-flat port list (SystemVerilog `struct`/packed-2D that Icarus rejects,
or tuple/bundle ports emitted as escaped `\a.b` identifiers), or whose outputs
are `X`/don't-care (no numeric signature). Those pairs have no `_tb` files.

Pairs that generate but whose `lhd sim` result disagrees with the golden are
tracked as `prp-simeq-<name>` **`fixme`** targets (see `_SIMEQ_FIXME` in
`inou/prp/BUILD`). Each is a real `inou.cgen.sim` gap the harness surfaces —
fix the codegen, not the test, to flip it green.
