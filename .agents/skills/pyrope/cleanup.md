# Cleaning up Pyrope

Turn machine-emitted or legacy Pyrope (from a Verilog/Chisel translation, an
emitter, or older code) into idiomatic Pyrope a person would write from the
design's intent. Establish which interfaces, timings and configurations the
translation must preserve and which changes the caller requested.

## 1. Before editing

* Identify the top, its external interface and who instantiates it
  (testbench, harness, parent design), the supported parameter
  configurations, existing checks, and known equivalence gaps.
* For a Chisel generator, trace the top through its parameters, optional
  children, bundle definitions and replication counts. An emitted module
  describes one elaboration. Check a second configuration to identify which
  widths, counts and choices belong in the generator.
* For equivalence checks, record the reference revision, top and parameters.
  Use the pre-edit Pyrope for a Pyrope refactor, or the matching RTL
  elaboration for a translation whose observable behavior must match.
* Run the applicable baseline checks before editing so
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
lhd pyrope fmt --verify -i file.prp
lhd pyrope style file.prp --diag-fmt pretty
lhd pyrope style dir/*.prp --emit diagnostics:style.jsonl
```

Format changed sources and testbenches after each edit pass. Run style on
those files and their affected callers.
For clock and reset findings, trace the port to the state it controls. Use
`Clock` and `Reset` for timing signals, keep ordinary names such as `clock`
and `reset` unquoted, and preserve the original edge, reset priority and
polarity. A clear driven by a transaction, exception or flush may be a normal
data-path operation. Check that case in simulation before retyping it.
Rerun style over the full source set after local fixes.

Also inspect repeated statement blocks (with
strides in literals and numbered identifiers) and flattened bundle arguments.
The style command exits 2 for suggestions and 1 for input failure. Check
the diagnostics for a partial parse even when it exits 0.
The suggested templates describe the pattern; they are not code to paste. Zero
findings says nothing about naming, types, structure or correctness.

## 3. What clean Pyrope looks like

**Structure**

* Organize by function; put state next to the logic that owns it. No wall of
  unrelated register declarations at the top of a file.
* Collapse numbered variants (`foo`, `foo_p1`, `foo_p2`) that differ only in
  widths or configuration into one generic (`mod foo<N=8>(a:Unsigned(bits=N))`).
  Use an untyped input when its shape follows the actual argument and the
  operation is the same at each width. Keep explicit types for architectural
  widths, signed arithmetic, clocks and resets. Update callers together and
  retain concrete wrappers only for entry points that consumers use.
* Check other tops, tests and formal properties before removing an apparently
  unused module. An assertion-only module can still observe protocol errors.
* Inline trivial helpers (one-hot mux, encoders, a one-stage delay) when a loop,
  reduction or single register says it more clearly. Keep a helper that has a
  real name in the design's vocabulary and more than one caller.
* Keep public names that imports, harnesses or proofs depend on. Group private
  logic by ownership, independently of how an emitter split it into files.
* One short comment per module saying what it does; comments at non-obvious
  logic saying why (priority order, reset intent, supported configuration).
  Do not narrate syntax.

**Interfaces and bundles**

* For a drop-in translation preserve the external port names, widths and types,
  including `U1` ports a harness drives with integers (convert to `Bool` inside:
  `x == 1`, `U1(flag)`). Two required repairs:
  - a physical clock port must be typed `Clock` (and a reset `Reset` where it
    is one); keep its name. Retyping can make the design multi-clock, after
    which every register needs `clock_pin=`.
  - a purely combinational top whose `clk`/`rst` drive no state becomes a
    `comb` without those ports (a `comb` cannot take `Clock`/`Reset`). An
    interface change like this needs a compatible wrapper if existing callers
    still use the original ports.
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
* Inside a loop, index arrays, not bit windows of a packed vector: declare
  per-lane ports and families as `[N]UW` (`io_in_a:[4]U8`,
  `const bCur:[16]U32 = [r0, r1, ...]`) and read `x[i]`, not
  `x#[(i*W)..<((i+1)*W)]`. Same logic, but in sim every slice is a
  shift-and-mask over the whole vector; a lane array is a plain read. Keep
  packing only at real bit-vector boundaries (above).
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

* Pass related choices as one compile-time record. For example, a child declared
  with `<C>` can use `Unsigned(bits=C.width)` at a port and `C.depth` for a
  register array extent. Forward the record as `child<C=C>(...)`. Give a
  public entry point its usual default binding and check legal combinations
  with `cassert` where the constraints are defined.
* Translate structural replication into `for i in 0..<N` over lanes or records.
  Preserve index direction, priority and read/write ordering. Check the index
  range for singleton and non-power-of-two dimensions. A bit mask implements
  wraparound only for a power-of-two depth.
* Put optional calls inside `if C.enabled { ... }` so the disabled
  specialization omits the child. For a runtime enable, put the stateful
  call inside `if active { ... }` only when the instance may hold state.
  A pipelined or handshaked child may need to drain accepted work, consume a
  response, or observe a wakeup after the initiating request goes away.
  Derive the guard from those obligations and define visible idle outputs.

For example, a caller can express both the configured presence of a child and
the cycles on which it may advance. Here the interface specifies zero when idle:

```pyrope
comptime const profile = (const enabled=true, const width=8, const depth=4)
mod wrapper<C=profile>(active:Bool, d:Unsigned(bits=C.width))
    -> (v:Unsigned(bits=4*C.width)@[0]) {
  v = 0
  if C.enabled {
    if active { v = lane_bank<C=C>(en=true, d=d) }
  }
}
```

## 4. Verify each step

* **Behavior-preserving edits** can be checked against the latest verified snapshot
  (compile each with `--set compile.upass.inline=false` and its own
  `--emit-dir lg:DIR`). On a
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
  exercises the design by checking accepted transactions and their payloads.
  For multiple clocks, vary the edges independently and check which domain
  advances while another is idle or in reset.
* **Configuration and interface changes.** Compile through the actual parent
  with default and alternate bindings. Exercise an enabled and disabled
  feature, and two differently specialized instances together when applicable.
  Simulate a transaction that distinguishes the promised behavior. For
  example, hold the first response until two requests have been accepted,
  and check their tags and data. For runtime activation, check idle,
  reactivation and reset while idle.
* **Suspected compiler bug.** Reduce the failing path while preserving what
  triggers it (for example, an imported config forwarded to a generic child).
  Pin the expected behavior in a regression at that stage. A simulator bug
  needs an observable value or timing check.
* Check the final formatting with the same formatter mode. For example, run
  `lhd pyrope fmt --verify file.prp -o "$tmpdir/check.prp" && cmp file.prp "$tmpdir/check.prp"`
  using a scratch directory. Repeat the behavioral checks on the formatted sources. Compare unchanged behavior at a common boundary. Use
  the new interface and its expected timing when testing a requested redesign.
* Never weaken assertions, change the reference, or alter the reference setup to
  get a pass. Translate checks on a changed internal interface to the new
  representation, keeping their assertions and stimulus.

## 5. Report

* Lead with reproducible language, compiler, emitter, formatter, simulation and
  verification issues, each with the smallest reproducer, expected behavior,
  command, diagnostic, and compiler identity. Separate real defects from source
  mistakes and from untested historical workarounds.
* For code size, list corresponding source blocks, tops and configurations.
  Record the formatter mode and measure final formatted sources.
  Use the same line-count convention, count each shared generator definition
  once, and report harnesses and tests separately. Measure build time and
  simulation throughput independently when reporting a performance change.
* Per design: final verdict against the reference, `formal.lec.match` pairs
  used, simulation comparison, landing-cycle changes, unsupported
  configurations, and workarounds left in the source.
