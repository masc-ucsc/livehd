Use the Pyrope skill to rewrite <source.prp or source directory> into
<destination.prp or destination directory> as clean, idiomatic Pyrope.
An equivalent Verilog or Chisel design may be available at <reference path,
or none>. Preserve the hardware behavior while reconsidering how it is expressed.

## 1. Before editing

- Identify the top, its external interface (who instantiates it: a testbench,
  a harness, another design), the supported parameter configuration(s), the
  existing checks, and any known equivalence gaps.
- Run the baseline checks first (compile, LEC against the reference,
  simulation) so existing failures are not mistaken for regressions. A
  machine-emitted Pyrope may itself be LEC-inconclusive against its Verilog; a
  cleaner rewrite often proves where the original could not.
- When using a Verilog reference, match its parameters and defines; for Chisel,
  use the RTL elaborated from the same configuration and record its provenance.
  With no external reference, the original Pyrope is the reference.
- Snapshot the starting Pyrope. Every later equivalence step compares against
  the most recent verified snapshot (section 4).

## 2. Find the repetition

Run the source-only style analyzer before editing and after each substantial
cleanup:

```sh
lhd pyrope style <file.prp> --diag-fmt pretty
lhd pyrope style <directory>/*.prp --emit diagnostics:/tmp/pyrope-style.jsonl
```

Inspect the highest-ranked findings first. The analyzer recognizes contiguous
repeated statement blocks (with strides in literals and numbered identifiers)
and flattened bundle arguments; its templates are descriptive, not executable
Pyrope. The default is 20 findings per file (`--max-findings`);
`--max-block-statements` exposes repetitions whose copies exceed 128
statements. Exit 2 means suggestions were reported, 1 an input/parser failure;
exit 0 can include a partial parse, so read the diagnostics. The analyzer only
finds repetition: zero findings says nothing about naming, types, structure, or
correctness, and it does not rewrite code or resolve imports.

## 3. What clean Pyrope looks like

Aim for code a person would write from the design's intent, not from the
emitter's netlist.

**Structure**

- Organize by function and place state next to the logic that owns it. Extract
  coherent modules and useful combinational helpers; avoid a block of unrelated
  register declarations at the top of a file.
- Consolidate numbered variants (`foo`, `foo_p1`, `foo_p2`) that differ only in
  widths, types, or configuration into one generic (`mod foo<N=..>(a:Unsigned(bits=N))`).
  Update callers to use it directly and delete the obsolete files. A shared
  body behind a family of wrappers is not the finished cleanup.
- Delete modules that elaborate to nothing: assertion-only checkers
  (`*_checks_*`, `*_intg`, `*_impl`), `br_misc_unused`/`tieoff` sinks, and the
  calls that fed them. Ports that only fed those checks (typically `clk`/`rst`
  on combinational children) go too; keep them on the external top.
- Inline trivial helpers (one-hot mux, bin/onehot encoders, a 1-stage delay,
  popcount of a 1-bit value) where a loop, reduction, or one register says the
  same thing more clearly. Keep a helper module when it has a real name in the
  design's vocabulary and more than one caller.
- Keep one module per file where the source did, named as before, so imports,
  test harnesses, and LEC hierarchy pairing keep working.
- Write a short comment per module saying what it does, and at non-obvious
  logic say why (priority order, reset intent, which configuration is
  supported). Do not narrate the syntax.

**Interfaces and bundles**

- Preserve the external top-level interface exactly: port names, widths, and
  types, including `U1` ports a generated harness drives with integers. Convert
  to `Bool` right inside the top (`pop_ready == 1`, `U1(flag)`). Built-in
  type names are capitalized (`U8`, `S4`, `Bool`, `Unsigned(bits=N)`); the
  old lowercase spellings (`u8`, `bool`, `unsigned`) are banned words, also
  as names.
- Intermediate interfaces may change. Pass related ports as tuples instead of
  escaped flattened arguments (`` `io_in.control.enable`=enable ``); reuse an
  existing tuple directly (`child(io_in=next_stage)`) or construct a named one
  (`child(io_in=(const control=(const enable=enable), const data=data))`).
  Change callee and callers together.
- Assign whole tuples instead of field-by-field copies (`control = decoder.control`);
  make producer and consumer agree on field names and nesting. A nested
  return does not bind to a flat destination, and a leaf name is not searched
  for in the tree.
- Before keeping pack/unpack helpers, check whether the consumer can take the
  unpacked bundle. Keep explicit packing only for real encodings, bit-vector
  operations, and external boundaries, and document lane order and widths
  there. A compiler failure on a valid bundle connection calls for a minimal
  reproducer and a LiveHD fix, not a packed interface (but see the workaround
  list in section 5).

**Expressions**

- Use loops, reductions, and whole-vector operations instead of per-bit
  statements: `bin#[i] = gray#^[i..]`, `count = in#+[..]`,
  `mask#[i] = last#|[i..]`, `lowest = v & -v`, `onehot = 1 << idx`.
- Derive widths and counts from declarations with `.[bits]`
  (`for i in 0..<gray.[bits]`, `comptime const N = request.[bits]`) instead of
  restating literals. `.[bits]` works on inputs and outputs; bind it to a
  `comptime const` before using it inside a `<...>` generic argument.
- Build a vector bit by bit in a typed local (`mut v:U16 = 0; v#[i] = ...`),
  then assign the output once.
- Use `if` expressions when every branch selects one destination's value
  (`x = if a { b } elif c { d } else { e }`). Preserve priority with
  `if`/`elif`; use `match` only when its parallel, mutually exclusive
  semantics are intended.
- Replace magic encodings, masks, field positions, and sizes with `comptime
  const` names defined where they belong (or `pub comptime const` in a shared
  file). Ordinary zero init, boolean conversions, and obvious arithmetic need
  no artificial constants.
- Use `Bool` and `true`/`false` for predicates and control. Keep integers for
  counters, encodings, and bit vectors, and make conversions explicit at the
  boundary (`U1(b)`, `x == 1`). The `int(...)` cast and the `int` type no
  longer exist.
- Use typed destinations and `wrap` for intentional fixed-width arithmetic
  (`mut limit:U12 = 0; wrap limit = base + size - 1`). Check signedness,
  extension, and shift behavior rather than treating every slice as a
  truncation.

**State**

- Declare reset value and pin with the register:
  `reg last:U16:[reset_pin=rst] = 0x8000` (sync, active-high by default;
  `negreset=true`, `async=true` otherwise; a reset's name, `_n` included,
  carries no polarity). Replace the emitter's
  `reg q___q` + `if rst {...} elif en {...}` with that plus `if en { q = d }`.
  Keep reset-dependent logic that does more than assign a reset value.
- A reset value must be comptime. Compute a structured one with a `comb`
  bound to a `comptime const` (`comptime const INIT = upper_triangle(size=N)`),
  not with a runtime `mut` loop.
- Clocks and resets bind by TYPE, not by name: registers bind the enclosing
  module's single `Clock` and `Reset` inputs implicitly, whatever their names
  (`clk:Clock`, `rst:Reset`), or mint `clock:Clock`/`reset:Reset` ports that
  the parent wires to its own. A `clk`/`rst` input typed `Bool`/`U1` is plain
  data. That is fine for a single-clock design, so a child that has no other
  use for its clock needs no clock port. In a multi-clock design (two or more
  `Clock` inputs) pass each clock explicitly and use `clock_pin=`/`reset_pin=`
  on every register. A `comb` never takes a `Clock`/`Reset` input.
- Do not turn enables or intentional holds into unconditional writes, and do
  not replace unknown or unreset state with convenient constants.
- Unused bits of a packed state vector (e.g. the lower triangle of a
  triangular matrix) should be driven exactly as the reference drives them,
  or LEC state pairing and induction will fail on them.
- Registered outputs of a `mod` land at `@[1]`, combinational ones at `@[0]`.
  Read the timecheck error: it usually names the right cycle. A caller that
  combines a `@[1]` output with same-cycle values without `stage[N]` alignment
  is rejected ("mixes values at different cycles"); the generated lhdtrack
  harnesses do exactly that, so on an external top declare such an output
  `@[]` and say why in a comment.

**Configuration**

- Use generics or `comptime const` for configuration and keep concrete entry
  modules where the flow requires them. Preserve supported configurations;
  mark unsupported ones with `cassert` and a comment
  (`cassert(NUM_REQUESTERS >= 2, "...")`). Do not add features.

## 4. Verify each step

Validate incrementally with LiveHD only.

- **LEC against the previous snapshot, not the original.** After each small
  change, compile and `lhd lec` the new Pyrope against the last verified
  snapshot; on a pass, the new version becomes the snapshot. Small deltas prove
  in seconds where a comparison against the original or the Verilog can take
  minutes or give up. Compile both sides with
  `--set compile.upass.inline=false` and compare `lg:` directories. When
  collapsing a field-by-field copy into a whole-tuple assignment, this is the
  check that it is equivalent.
- **Verdicts.** `proven` is a pass and `refuted` is a failure. A `timeout` or
  `inconclusive` is not a disproof and may be accepted, **but it is not a proof
  either**: an inconclusive LEC has hidden real miscompiles in this flow. Every
  step that is not `proven` must be backed by simulation.
- **Renamed state.** Changed hierarchy or instance names break automatic
  flop pairing and leave LEC inconclusive. Pair renamed registers explicitly
  with `--set formal.lec.match='ref.path.q=impl.path.q'` (same widths only; a
  width mismatch yields a spurious refute). Instance names come from the
  binding (`const arb = child(...)` names the instance `arb`). Keep the mapping
  in the report and separate from automatic matching.
- **Simulation.** Run the existing testbench on the original and the cleaned
  source for enough cycles to get past reset into useful work, and compare
  results. Check that the testbench actually exercises the design: a harness
  that ties a handshake input to a constant (or a recorded checksum of 0)
  makes the simulation gate vacuous; drive it randomly in a scratch harness
  and compare original vs cleaned there. Simulation cannot see clock-domain mistakes when the harness ties
  all clocks together; multi-clock designs need LEC.
- **Packed or split state** cannot be paired (`formal.lec.match` maps whole
  flops only). Prefer keeping the reference's state granularity; when a
  cleanup must repack state, expect an inconclusive LEC and lean on
  simulation plus a proven intermediate step.
- **Formatter.** `lhd pyrope fmt` defaults to AI mode (one line per
  statement, sorted named lists, `f(x=x)` → `f(x)`). It can still change
  meaning when it sorts a `type`/`enum` declaration or shortens an argument
  that names no parameter, so LEC the formatted source against the
  pre-format snapshot every time (the pre-format → formatted step is cheap).
- **Final checks.** Run `lhd pyrope fmt -i`, then LEC the formatted source
  against the pre-format snapshot, LEC the final source against the external
  reference at the unchanged top, and rerun simulation. Exercise the
  representative parameter combinations the cleanup touched. Prefer an
  unbounded (inductive) proof when the original had one.
- Only the external top must stay equivalent; intermediate modules may change
  boundaries. Keep the same top and testbench across Verilog, generated
  Pyrope, and cleaned Pyrope. Where the project uses incremental compilation
  or precompiled imports, check those paths too.
- Do not weaken assertions, alter the reference, or change benchmark settings
  to get a pass. Translate existing checks of a changed internal interface to
  the new representation, preserving their assertions, stimulus, and
  coverage.

## 5. Known LiveHD limitations (check before trusting a workaround)

Re-verified 2026-09-28. Retest each before relying on it, and report any that
are fixed or new with a minimal reproducer. Do not carry an old workaround
into new code without re-checking it: most of the 2026-09-27 list is fixed
(generic `comb` output widths, loop-read generic port widths, the false
combinational loop through a child's register, bit writes into outputs,
nested lambdas reading comptime constants, `wire`/`reg`-array use in loops,
byte-enable memory writes, `a#[0..+1]`, single-output auto-unwrap, tuple-typed
ports and fields, stateful children in loops under `lhd sim`), so
`::[timecheck=false]`, `mod`-instead-of-`comb`, typed rebinds, and
build-then-assign locals are no longer needed.

- **Silent:** a register both bit-read and conditionally bit-written inside a
  rolled `for` loop loses the write; build the next value in a `mut` and
  assign the register once after the loop. **Silent:** an
  `ordering="old"` memory keeps only the last of several same-cycle partial
  writes to one entry; write the merged word. Both passed compile and one
  passed sim; only LEC or the full simulation caught them.
- A memory element passed to a typed `comb` input now fails ("unbounded
  range"); bind it to a typed `const` first. In a generic lambda with any
  `mut`, cast a local (`const b = flag; U1(b)`) rather than a `Bool` port.
  An import const must not share its file's name.
- LEC budget is not monotone: a design that is inconclusive at
  `formal.timeout=30` can prove in seconds at 150. Retry a surprising
  inconclusive result with a larger budget before restructuring.
- When the reference stores a triangular or partially-used state vector in
  one flop (unused bits hold, never reset), the proof needs the Pyrope to
  mirror that; an idiomatic reset of the unused bits stays inconclusive.
- Arrays whose element width is generic (`reg r:[N]Unsigned(bits=N)`, array
  ports of generics) are rejected, and a bit-assign into an array element
  inside nested loops (`m[i]#[j] = ...`) fails. Pack as `Unsigned(bits=N*W)`
  with row slices `#[(i*W)..+W]` (or build a row in a scalar and assign
  `m[i] = row`) and document the layout.
- `std.clog2` and value-derived `.[bits]` of a comptime constant are
  documented but not implemented; pass derived widths as extra generics with
  a `cassert`, or use `Unsigned(max=DEPTH - 1)` for index types.
- Write an expression used as a generic argument in parentheses
  (`m<W=(2*N - 1)>`).
- Range analysis does not use the guarding condition:
  `x = if x == MAX { 0 } else { x + 1 }` on `Unsigned(max=MAX)` still needs
  `wrap`.
- Auto-generated instance names of unnamed calls are long mangled strings;
  bind calls whose state LEC must pair (`const sync = f(...)`) so instance
  names stay predictable.
- Unverified since the fix pass: `lhd sim` reading a `wire` back-edge one
  cycle late inside a `for` loop. Back loop refactors with a full simulation.

## 6. Report

Report code size against both the starting Pyrope and the matching Verilog,
including every helper introduced or removed, with the same word-count method
and source scope on both snapshots. Separate formatter-only changes from
reductions in repeated logic; fewer words are evidence of a simpler
representation, not a reason to compress whitespace or weaken validation.
List per test: final LEC verdict against the reference, any `formal.lec.match`
pairs used, the simulation comparison, unsupported configurations, and
LiveHD workarounds left in the source.
