Use the Pyrope skill to rewrite <source.prp or source directory> into
<destination.prp or destination directory> as clean, idiomatic Pyrope.
An equivalent Verilog or Chisel design may be available at <reference path,
or none>. Preserve the hardware behavior while reconsidering how it is expressed.

## 1. Before editing

- Identify the top, its external interface (who instantiates it: a testbench,
  a harness, another design), the supported parameter configuration(s), the
  existing checks, and any known equivalence gaps.
- When Verilog is available, test the CURRENT emitter before editing:
  `lhd compile verilog --top TOP --emit-dir pyrope:tmp/generated -- -F FILELIST
  -DSYNTHESIS -DBR_PPA_SYNTHESIS` (use the reference's actual defines).
  Compile the emitted top in its untouched helper tree, then LEC it against
  the same elaborated reference. Also test re-emission of the cleaned Pyrope
  itself when evaluating writer behavior. Emission success alone is not a round-trip
  pass: generated Pyrope may be unattractive, but must compile and preserve
  behavior. Preserve invalid generated output and reduce the source that
  produced it. Do not silently fix it and report the emitter as passing.
  Pyrope emission takes source or `ln:` input, not `lg:` (there is no
  LGraph-to-LNAST decompiler).
- Run the baseline checks first (compile, LEC against the reference,
  simulation) so existing failures are not mistaken for regressions. A
  machine-emitted Pyrope may itself be LEC-inconclusive against its Verilog; a
  cleaner rewrite often proves where the original could not.
- When using a Verilog reference, match its parameters and defines; for Chisel,
  use the RTL elaborated from the same configuration and record its provenance.
  With no external reference, the original Pyrope is the reference.
- For file-preloaded memories, run checks with the original image paths
  resolvable. Current `lgcheck` enables `LIVEHD_FORMAL_MEMORY_MODEL` to read
  emitted behavioral memory models through their default synthesis black-box
  boundary. A memory-limit termination is an error, not a timeout or proof;
  reduce bounded-search unrolling or schedule the check with more memory.
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
  old lowercase spellings are no longer built-in types or casts, but remain
  legal ordinary identifiers. Reserved-word matching is case-sensitive;
  `clock` and `reset` are legal names, while `Clock` and `Reset` need
  backticks when used as names. Physical clock ports must have type `Clock`;
  correcting an old data-typed clock is a required semantic repair. Preserve
  its physical name and width, and wire it consistently through the harness.
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
  data and cannot drive `clock_pin`. A child may omit its clock argument only
  when implicit typed-clock wiring selects the intended domain; removing a
  clock port does not remove the need to verify its physical connection.
  In a multi-clock design (two or more
  `Clock` inputs) pass each clock explicitly and use `clock_pin=`/`reset_pin=`
  on every register. A `comb` never takes a `Clock`/`Reset` input.
- Do not turn enables or intentional holds into unconditional writes, and do
  not replace unknown or unreset state with convenient constants.
- Unused bits of a packed state vector (e.g. the lower triangle of a
  triangular matrix) should be driven exactly as the reference drives them,
  or LEC state pairing and induction will fail on them.
- Conditional/feedback state outputs commonly land at `@[0]`; a direct
  read of a body register written unconditionally from inputs lands at `@[1]`.
  The blanket rule "all registered outputs are cycle 1" is wrong. Declare
  intentional timing and read the timecheck error. A caller that
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

Validate incrementally with LiveHD, then check emitted Verilog independently with Yosys-backed `lgcheck`.

- **LEC after each significant Pyrope edit.** Compile and `lhd lec` the new
  Pyrope against the most recent verified Pyrope snapshot; on a proof, the new
  version becomes the snapshot. These incremental comparisons usually prove
  faster than comparing every edit against the original Pyrope or Verilog,
  because the changes between the two versions are smaller. Keep the original
  reference for the baseline and final checks. Compile both sides with
  `--set compile.upass.inline=false` and compare `lg:` directories. When
  collapsing a field-by-field copy into a whole-tuple assignment, this is the
  check that it is equivalent.
- **Suspected LiveHD bug.** Run Yosys-backed `inou/yosys/lgcheck` as an
  independent cross-check when you suspect a LiveHD compiler, emitter, or LEC
  bug; do not wait for the final cleanup check. Use the original Verilog
  reference when available, with matching parameters, defines, and memory
  images. Preserve the reproducer and both tools' verdicts when they disagree;
  two LiveHD-emitted sides can share a lowering defect.
- **Verdicts.** Read `lec.verdict`, `lec.bounded`, and `lec.bound` together.
  `proven` with `bounded=true` proves only the recorded depth; it is not an
  unbounded equivalence result. Report that distinction per test. `refuted`
  is a failure. Apply the requested time budget to the entire check and its
  subprocesses. For the lhdtrack cleanup gate, accept a proof or a genuine
  timeout at three minutes; never accept a refutation. Record timeouts
  separately from proofs. An early inconclusive result, unsupported operation,
  setup failure, or missing result JSON is not a timeout. Every step that is
  not proven must also be backed by simulation.
- **Renamed state.** Changed hierarchy or instance names break automatic
  flop pairing and leave LEC inconclusive. Pair renamed registers explicitly
  with `--set formal.lec.match='old_instance.q=new_instance.q'` (same widths only; a
  width mismatch yields a spurious refute). Instance names come from the
  binding (`const arb = child(...)` names the instance `arb`). Keep the mapping
  in the report and separate from automatic matching. The left name belongs
  to the reference and the right to the implementation; `ref.` and `impl.`
  are NOT special prefixes. Use real hierarchical or canonical state names.
  Current LiveHD rejects unresolved explicit state names before starting the
  proof. Keep correspondence scoped to the actual pair being checked; a
  regenerated hierarchy can make an older mapping obsolete.
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
  statement, sorted named lists, `f(x=x)` → `f(x)`). Recheck semantics and
  idempotency instead of assuming all historical formatter bugs remain.
  The type/enum declaration-order reproducer preserves order on the
  2026-09-30 build. LEC the formatted source against the pre-format snapshot
  every time (the pre-format → formatted step is cheap).
- **Final checks.** Run `lhd pyrope fmt -i`, then LEC the formatted source
  against the pre-format snapshot, LEC the final source against the external
  reference at the unchanged top, and rerun simulation. Exercise the
  representative parameter combinations the cleanup touched. Also compile the
  final Pyrope with `--emit verilog:tmp/net.v`, then compare that Verilog against
  the complete original Verilog with both `lhd lec` and Yosys-backed `lgcheck`.
  Feed `lgcheck` the original sources, defines, and memory images through its
  supported reader; using LiveHD-emitted Verilog as both sides can hide a shared
  lowering defect. Use the same three-minute ceiling for each equivalence
  check. Prefer an unbounded (inductive) proof when the original had one.
- Only the external top must stay equivalent; intermediate modules may change
  boundaries. Keep the same top and testbench across Verilog, generated
  Pyrope, and cleaned Pyrope. Where the project uses incremental compilation
  or precompiled imports, check those paths too.
- Do not weaken assertions, alter the reference, or change benchmark settings
  to get a pass. Translate existing checks of a changed internal interface to
  the new representation, preserving their assertions, stimulus, and
  coverage.

## 5. Known LiveHD limitations (check before trusting a workaround)

The 2026-09-30 round-4 writer fixes address invalid Bool initialization,
forward-wire double drivers, output slice reads, expanded local scopes,
retained-loop names, generic imports and bounds, and clock classification
through wrappers. The sized cast of an expression built from scalar tuple
fields now compiles. Explicit LEC match names are validated. Keep checking the
current build: a focused regression passing is not a guarantee for all programs.

**Writer checks to retain**

- Compile untouched emitted trees, format them, compile again, and run the
  external-reference equivalence checks. Keep raw output and diagnostics for
  failures; successful emission alone is insufficient.
- Exercise Pyrope-to-Pyrope emission as well as Verilog-to-Pyrope emission.
  Generic compile-time helpers and loops can follow different writer paths.
- When repairing LiveHD, run its normal test suite and focused debug emitter
  and clock checks. Include constant/generic tuple cases and genuine runtime
  overflow rejection; retain the existing assertions and stimulus.
- A writer refusal must produce a structured unsupported result, not abort.
  Sparse or unrecoverable runtime masks may still be unsupported; save a
  reduced case without changing correct source just to hide the refusal.
- Memory initialization is observable behavior. Preserve image paths and
  runtime-versus-source-relative path semantics, and make the same image
  available to all compilers and equivalence engines.
- Clock-aware `lgcheck` can need structural state correspondence after lowering
  clocks to global sampling. Every proposed pair must still be proved, with
  power-on compatibility checked; do not bypass clock/reset guards. If bounded
  checking finishes inconclusive, `LGCHECK_INDUCT_AFTER_BMC=1` can try whole-miter
  induction with the remaining shared budget. It adds no guessed internal
  correspondence assumptions, and an induction failure is not a refutation.

**Old workarounds that passed their focused rechecks**

- A register bit-read and conditionally bit-written in a rolled loop LEC-proves
  against scalar writes; no automatic `compile.unroll=true` workaround.
- Multiple same-cycle partial writes to an `ordering="old"` memory LEC-prove
  against a merged-word update.
- Generic-width register arrays and a memory element passed directly into a
  typed `comb` compile in the saved probes; test the actual design before
  retaining packed-vector or typed-rebind workarounds.
- `std.clog2` and `.[bits]` on a comptime integer pass compile-time assertions.
- The formatter preserves field order in the tested `type` and `enum`, and
  formatting all 20 cleaned designs LEC-proves against their pre-format forms.

Do not treat untested entries from earlier suggestions as current defects.
Retest the exact pattern before applying a workaround. Preserve state layout
when proof pairing depends on it; arbitrary reset changes to previously
unreset bits still change the implementation. For simulator-specific loop or
clock concerns, run the recorded full-cycle workload as well as LEC. The
lhdtrack `suggestions4.md` and `suggestions4_repros/` contain current evidence.

## 6. Report

Lead the feedback with reproducible Pyrope syntax/semantics, LiveHD emitter,
compiler, formatter, simulation, and verification issues. Include anything
still difficult or unresolved. Separate actual defects from source mistakes,
retired workarounds, and untested historical reports. For each defect give the
smallest reproducer available, expected behavior, command, result JSON or
diagnostic, and compiler identity. Keep legacy migration as brief setup
context; it is not the focus of the feedback.

Report code size against both the starting Pyrope and the matching Verilog,
including every helper introduced or removed, with the same word-count method
and source scope on both snapshots. Separate formatter-only changes from
reductions in repeated logic; fewer words are evidence of a simpler
representation, not a reason to compress whitespace or weaken validation.
List per test: final LEC verdict against the reference, any `formal.lec.match`
pairs used, the simulation comparison, unsupported configurations, and
LiveHD workarounds left in the source.
