# Cleaning up Pyrope

Turn machine-emitted or legacy Pyrope (from a Verilog/Chisel translation, an
emitter, or older code) into idiomatic Pyrope a person would write from the
design's intent. The external behavior must not change; how it is expressed
should.

## 1. Before editing

* Identify the top, its external interface and who instantiates it
  (testbench, harness, parent design), the supported parameter
  configurations, existing checks, and known equivalence gaps.
* Pick the reference: the original Verilog/Chisel RTL elaborated with the same
  parameters and defines, or, with no external reference, the original Pyrope.
  Record its provenance.
* Run the baseline first (compile, LEC against the reference, simulation) so
  pre-existing failures are not mistaken for regressions. Machine-emitted
  Pyrope can itself be LEC-inconclusive against its source; a cleaner rewrite
  often proves where the original could not.
* When the source came from an emitter, also check the emitter: compile the
  untouched emitted tree, format it, compile again, and LEC it. Emission success
  alone is not a round-trip pass. Keep invalid emitter output as a reproducer
  instead of silently fixing it.
* Make every memory image the design loads resolvable by every tool.
* Snapshot the starting Pyrope. Each later equivalence step compares against
  the most recent verified snapshot (section 4).

## 2. Find the repetition

```sh
lhd pyrope style file.prp --diag-fmt pretty
lhd pyrope style dir/*.prp --emit diagnostics:style.jsonl
```

Start with the highest-ranked findings: repeated statement blocks (with
strides in literals and numbered identifiers) and flattened bundle arguments.
The suggested templates describe the pattern; they are not code to paste. Zero
findings says nothing about naming, types, structure or correctness.

## 3. What clean Pyrope looks like

**Structure**

* Organize by function; put state next to the logic that owns it. No wall of
  unrelated register declarations at the top of a file.
* Collapse numbered variants (`foo`, `foo_p1`, `foo_p2`) that differ only in
  widths or configuration into one generic (`mod foo<N=8>(a:Unsigned(bits=N))`).
  Update callers and delete the obsolete files; a shared body behind a family
  of thin wrappers is not finished.
* Delete what elaborates to nothing: assertion-only checker modules, tie-off
  and unused-signal sinks, and the ports and calls that only fed them.
* Inline trivial helpers (one-hot mux, encoders, a one-stage delay) when a loop,
  reduction or single register says it more clearly. Keep a helper that has a
  real name in the design's vocabulary and more than one caller.
* Keep one module per file where the source did, with the same names, so
  imports, harnesses and hierarchical proofs keep pairing.
* One short comment per module saying what it does; comments at non-obvious
  logic saying why (priority order, reset intent, supported configuration).
  Do not narrate syntax.

**Interfaces and bundles**

* Preserve the external top interface exactly: port names, widths and types,
  including `U1` ports a harness drives with integers (convert to `Bool` inside:
  `x == 1`, `U1(flag)`). Two required repairs:
  - a physical clock port must be typed `Clock` (and a reset `Reset` where it
    is one); keep its name. Retyping can make the design multi-clock, after
    which every register needs `clock_pin=`.
  - a purely combinational top whose `clk`/`rst` drive no state becomes a
    `comb` without those ports (a `comb` cannot take `Clock`/`Reset`). An
    unconnected clock is not an equivalence difference; regenerate or adjust
    the harness accordingly.
* Reserved-word port names keep their backticks.
* Internal interfaces may change. Pass related signals as tuples instead of
  flattened escaped arguments; reuse an existing tuple (`child(io_in=next)`) or
  build a named one. Change callee and callers together.
* Assign whole tuples instead of field-by-field copies; make producer and
  consumer agree on field names and nesting.
* Keep explicit bit packing only for real encodings, bit-vector operations and
  external boundaries; document lane order and widths there. A compiler failure
  on a valid bundle connection calls for a reproducer, not a packed interface.

**Expressions**

* Loops, reductions and whole-vector operations instead of per-bit statements:
  `bin#[i] = gray#^[i..]`, `count = v#+[..]`, `mask#[i] = last#|[i..]`,
  `onehot = 1 << idx`.
* Derive widths and counts from declarations (`x.[bits]`, `std.clog2(N)`)
  instead of restating literals; bind them to a `comptime const` before use as
  a generic argument.
* Build a vector bit by bit in a typed local, then assign the output once. A
  concatenation needs a typed destination (`const w:U12 = (a, b)#[..]`).
* `if` expressions when every branch selects one destination's value. Keep
  priority with `if/elif`; use `match` only when parallel, mutually exclusive
  arms are intended.
* Name magic encodings, masks, field positions and sizes with `comptime const`
  where they belong. Plain zero init and obvious arithmetic need no names.
* `Bool` for predicates and control, integers for counters and vectors, with
  explicit conversions at the boundary.
* Typed destinations and `wrap` for intentional fixed-width arithmetic; check
  signedness, extension and shifts instead of treating every slice as
  truncation.
* Prefer the cheaper equivalent formulation when it is also the clearer one
  (see `synth.md`, "Source rewrites"): a mask from a prefix scan instead of
  `v & -v`, an AND-OR select over a decoded index instead of a wide shift.

**State**

* Declare reset value and pins on the register:
  `reg last:U16:[reset_pin=rst] = 0x8000` (sync, active-high by default;
  `negreset=true`, `async=true` otherwise; a `_n` name carries no polarity).
  `reset_pin`/`clock_pin` name a signal, not an expression: name a computed
  reset first (`const any_rst = rst or soft`). Replace emitted
  `if rst {...} elif en {...}` blocks with that plus `if en { q = d }`.
* Reset values must be comptime; compute a structured one with a `comb` bound
  to a `comptime const`.
* Clocks and resets bind by type. In a multi-clock design pass each clock
  explicitly and give every register its `clock_pin=`/`reset_pin=`.
* Never turn an enable or an intentional hold into an unconditional write, and
  never replace unknown or unreset state with a convenient constant.
* Drive unused bits of a packed state vector exactly as the reference does, or
  proof state pairing fails on them.
* Landing cycles: conditional or feedback state outputs commonly land at
  `@[0]`; a body register written unconditionally and read directly lands at
  `@[1]`. Declare intentional timing and read the timecheck error. The declared
  landing cycle of a top output is part of its interface: tighten `@[]` only
  when the harness still compiles, never loosen `@[N]` to `@[]`, and list every
  change.

**Configuration**

* Generics or `comptime const` for configuration; keep the concrete entry
  modules the flow needs. Mark unsupported configurations with `cassert` and a
  comment. Do not add features.

## 4. Verify each step

* **LEC after every significant edit**, against the latest verified snapshot
  (compile both with `--set compile.upass.inline=false --emit-dir lg:DIR`). On a
  proof, the new version becomes the snapshot. Keep the original reference for
  the baseline and final checks.
* **Verdicts**: `proven` (check `bounded`/`bound`), `refuted` (a failure, never
  accepted), `unknown` (timeout or inconclusive: record separately and back
  with simulation). Apply the time budget to the whole check.
* **Renamed state**: pair it with `formal.lec.match` (equal widths only, real
  hierarchical names, reference on the left). Keep the reference's state
  granularity when you can; repacked state cannot be paired.
* **Simulation**: run the existing testbench on the original and the cleaned
  source past reset into useful work and compare. Make sure the stimulus
  exercises the design (a handshake tied to a constant, or a checksum of 0,
  makes the comparison vacuous); add random stimulus in a scratch harness when
  needed. Simulation cannot see clock-domain mistakes when all clocks are tied
  together; multi-clock designs need LEC.
* **Suspected compiler bug**: cross-check with an independent tool (a
  Yosys-based equivalence check on the original sources). Keep the reproducer
  and both verdicts when they disagree.
* **Final checks**: `lhd pyrope fmt -i`, prove formatted vs pre-format, prove
  the final source against the external reference at the unchanged top, rerun
  simulation, and exercise the parameter combinations the cleanup touched. For
  a Verilog-backed cleanup also emit Verilog from the final Pyrope and check it
  against the original sources with an independent tool.
* Never weaken assertions, change the reference, or alter benchmark settings to
  get a pass. Translate checks on a changed internal interface to the new
  representation, keeping their assertions and stimulus.

## 5. Report

* Lead with reproducible language, compiler, emitter, formatter, simulation and
  verification issues, each with the smallest reproducer, expected behavior,
  command, diagnostic, and compiler identity. Separate real defects from source
  mistakes and from untested historical workarounds.
* Code size against the starting Pyrope and the reference, same counting
  method on both, helpers added or removed included; separate formatter-only
  changes from real simplification.
* Per design: final verdict against the reference, `formal.lec.match` pairs
  used, simulation comparison, landing-cycle changes, unsupported
  configurations, and workarounds left in the source.
