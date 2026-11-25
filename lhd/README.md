# lhd developer tools

`lhd` is the stateless driver for compile, simulation, equivalence, synthesis, and detailed routing.
Use `lhd help` or `lhd describe <command>` for the command's accepted arguments.

`lhd tool sta lg:net --top top` queries saved critical-path annotations, with
filters such as `clock=core`, `'slack<0'`, `color=3`, and `src=alu.prp`.
Generate the annotations with `lhd pass opentimer ... --set
pass.opentimer.critical=true` (also accepted by `lhd synth`). The default margin
is 5% of the target clock period; targets come from SDC or
`--set pass.opentimer.targets=core:1ns,bus:4ns`. See
[STA annotations and queries](../pass/opentimer/README.md#critical-path-annotations-and-queries).

## Detailed routing

Build `//lhd` and use the DEF bundle exported by `lhd synth --emit-dir def:placed` as the routing input. The bundle records the order of the technology, cell and macro LEFs in `lefs.json`. An explicit frame can also be passed as `def:design.def lef:tech.lef lef:cells.lef`.

```sh
lhd route def:placed --workdir flat
lhd route chunk def:placed plan:fp_chunks.json --set lhd.jobs=4 --workdir W
lhd route repair def:placed route:W/route --set route.pin_neighbors=true --workdir W --emit-dir def:routed
lhd route verify def:routed
```

For a rectangular grid, use `--set route.grid=2x2` in place of the placement plan. The chunk coordinator schedules independent workers using the same `lhd` executable. With `--gui`, the resident router accepts placement-style agent requests at `W/route.sock` until the agent sends `finish`. In a second terminal, use `tools/lhd_gui.py --socket W/route.sock status`.

Every completed invocation returns the standard result envelope. The `qor.complete` flag requires zero open nets and zero native geometry findings. Inspect `qor.unrouted` and `qor.violations` for the counts. `lhd route verify` checks the existing wires. A named workdir keeps its findings under `W/route-verify`. `--result-json FILE` saves the envelope. `W/route` points to the latest completed checkpoint. Earlier trials remain in `W/route-runs`. Repeating a chunk operation in the same workspace reuses compatible worker packets. Use `reuse:CHECKPOINT` to choose a specific source or `--set lhd.incremental=false` for fresh workers. `lhd route --help` and `lhd list options route` describe the routing settings.

## ABC-free build

The normal build retains ABC for alternative synthesis and optional services.
Build the same CLI without those dependencies with:

```
bazel build -c opt --define=livehd_abc=false //lhd
./bazel-bin/lhd/lhd synth design.v --top top --set synth.mapper=usyn --set pass.usyn.tmap=none --emit verilog:logical.v
```

This configuration omits the ABC mapper/tmap provider, ABC-backed Liberty model
generation, and Yosys integration (whose kernel links ABC). Native Slang/Pyrope
compilation, USYN logical synthesis, and CVC5 remain available. The optional ABC
cone accelerator returns `Unsupported` for every obligation, leaving those
obligations with CVC5. No missing accelerator can produce a proven cut.
`pass.satopt` stays off unless explicitly enabled.

`python3 scripts/check_usyn_noabc.py --mode opt` runs the dependency removal audit:
it copies current sources to a disposable directory, removes `pass/abc`, its
package files and the ABC repository declaration, audits the configured `lhd`
dependency closure, and builds the CLI there. The checkout stays
unchanged; the fixture and build evidence are retained. This explicit build gate
is outside the default test suite.

## Formal regression policy

Ordinary regressions use `lhd lec` with the default solver and native Slang
reader. `formal.strict`, `formal.allow_oversize`, `pass.abc.allow_oversize`,
`formal.lec.gold_reader`, and `formal.lec.gate_reader` are removed options
(setting one is a usage error). `lhd lec` and `lhd formal verify` print the
same verdict words and exit with the same codes; every verdict carries a
modifier and "unknown" is never printed:

| verdict | meaning | exit |
| --- | --- | --- |
| `PROVEN-full` | unbounded (inductive) proof | 0 |
| `PROVEN-bounded(n)` | no difference / violation for n design cycles from reset | 0 |
| `TESTED-sampled(N)` | a sampled `formal.effort` preset: N vectors agreed | 0 |
| `TIMEOUT-time` | a clock limit (formal.timeout, wall deadline, solver budget) | 0 + warning |
| `TIMEOUT-memory` | out of memory, caught instead of a crash | 0 + warning |
| `REFUTED-cex` | a real counterexample (a boxed model is the contract) | 10 |
| `REFUTED-interface` | a top port name exists on one side only (lec) | 7 |
| `REFUTED-unsupported` | no engine can model or check it | 7 |
| `REFUTED-unchecked` | the options pinned an engine or disabled the deciding step | 7 |
| `REFUTED-vacuous` | the check proved nothing: nothing to compare (no output/state), no assert, a dead `if`/`match` guard, or a contradictory assume set (it can hide a real counterexample) | 7 |
| `REFUTED-unexplained` | no recognized cause: a LiveHD bug to report | 7 |

The machine-readable forms are lowercase (`lec.verdict` / `formal.verdict` in
`--result-json`, `run.verdict` / `obligations[].verdict` in `formal_report.json`). There is no
design-size gate: a run that outgrows memory reports `TIMEOUT-memory`. Bounded
proofs disclose their depth; cache hits preserve it and reject legacy records
without it, and only `TIMEOUT-time` results enter the known-timeout ledger.
The explicit debug setting `formal.solver=lgyosys` also runs default native
LEC and requires agreement with lgcheck: a native `TIMEOUT-*` adopts lgcheck's
decision when it has one, a native `REFUTED-cex` stands (with a warning) when
lgcheck cannot decide, opposite decisions are an internal DISAGREE error, and a
native proof that lgcheck cannot confirm is `REFUTED-unchecked`.
One regression tier additionally calls `inou/yosys/lgcheck` DIRECTLY as a second
opinion: `//inou/prp:prp-equiv-*` (Pyrope-generated Verilog vs its hand-written
golden), so a native LEC that ever proved nothing cannot go unnoticed across
that corpus. There a yosys refutation fails the test while an undecided or
timed-out yosys is tolerated — see `inou/prp/tests/equiv/README.md`. Every other
LEC in the repository stays on the default cvc5 solver.

The Yosys importer regressions explicitly compile through Yosys and require
both `lgcheck` and native `lhd lec` on each checked round trip. The `nocheck_`
fixtures retain their existing compilation-only scope.

Memory entry and bulk writes preserve their process clocks, including uniform
negative edges. Mixed-edge memories retain an explicit marker in LGraph;
formal checking and Verilog emission reject that unsupported schedule. Bulk
updates combined with entry writes require one shared clock and edge.
Native formal also rejects uniform falling-edge memory schedules, although
their Verilog emission is supported and covered by RTL simulation.

## Inspecting simulator code

Run `lhd sim <inputs> --setup-only --workdir W --set sim.dump_bitcode=true`
to save the pre-optimization LLVM bitcode beside each object in `W/sim/`.
For example, `llvm-dis W/sim/drv.tb.bc -o -` prints the testbench IR.
The DUT manifest lists the emitted objects. Each has a matching `.llvm.bc`.
When several scheduled kernels use the same computation, the object contains
one shared function and an entry point for each occurrence. Each entry binds
the addresses for that occurrence's state. Registers and memories of repeated
module definitions have consistent offsets within their instance and keep
independent values. A trace-enabled setup also writes `drv.tb.trace.bc`.
Use the LLVM major version pinned in `MODULE.bazel`.

## Simulator checkpoints and observation

The LLVM testbench runner saves the DUT state and testbench locals during
top-level `tick` loops. Set `sim.checkpoint_every=N` for a fixed cadence, or
use the default time-based policy. `sim.checkpoint_max=0` retains every
snapshot. `--restart-cycle N` loads the nearest compatible snapshot at or
before N. The testbench's `clock` variable still starts at zero in each
`tick`, while replay uses an absolute cycle number across the test.

`--list-signals` enumerates the DUT fields of a selected test. Use
`--probe SIG,...` for a per-cycle window, `--break-when 'SIG OP VALUE'` for the
first matching cycle, and `--query FILE|-|JSON` for a version 1 query batch.
Queries support `signals`, `value`, `values`, `snapshot`, `diff`, `changes`,
`next_change`, and `find`. A query can address a memory word by index or use
the first failing assertion as a cycle anchor. Query outputs describe
combinational outputs during the period and state after the step.

VCD and value observations replay only the required checkpoint intervals.
`--set lhd.jobs=N` bounds concurrent interval replay. If a requested range
precedes the first usable checkpoint, replay begins from power-on.
`--vcd-from` and `--vcd-to` select an inclusive cycle window. VCD timestamps
follow the absolute step timeline, with 10 time units per step.

`--setup-only` emits `drv.tb.trace.o` when given a VCD, probe, breakpoint or
query option. Keep that artifact for later observation with `--run-only`.
The ordinary simulation object runs the test and determines its verdict.
A tick drives unbound root clock ports together. Setup names those ports when a
DUT has several clocks. An explicit waveform on one of several root clocks
requires independent clock scheduling and produces an unsupported diagnostic.

Incremental setup verifies native objects against their inputs and on-disk
contents before reuse. The generated-driver profiler, fake VCD delay and
`end_digest` / `out_digest` result fields were retired with the C++ driver.
The CLI still recognizes the legacy tuning vocabulary for existing configurations.

## Pyrope formatter

```sh
./bazel-bin/lhd/lhd pyrope fmt file.prp                          # AI layout to stdout
./bazel-bin/lhd/lhd pyrope fmt -i file.prp                       # rewrite in place
./bazel-bin/lhd/lhd pyrope fmt file.prp --mode human --width 100 # wrapped, aligned
```

`pyrope fmt` embeds prpfmt (`prpfmt_format_string_mode`). The default
`--mode ai` has no width limit and no vertical alignment, so an edit changes
only the lines it touches -- except that an `if`/`elif`/`else` expression chain
longer than the default 132 columns wraps between its branches (the Human
layout); `const comptime` prints as `comptime const`; `--mode human` wraps at `--width` (default 132) and
aligns consecutive same-kind assignments. `--indent` applies to both modes;
`--width` in AI mode has no effect and warns (`width-ignored`). `--verify`
re-parses the output.

## Pyrope style suggestions

```sh
./bazel-bin/lhd/lhd pyrope style file.prp --diag-fmt pretty
./bazel-bin/lhd/lhd pyrope style file.prp --emit diagnostics:style.jsonl
```

`pyrope style` parses the source with Tree-sitter, without compiling or resolving
imports. It reports repetition and five structural cleanup opportunities. All
findings are advisory; they do not establish that a replacement is equivalent.

The repetition detector finds **contiguous repeated sequences of statements within a scope**.
One copy can include several declarations, assignments, and nested `if` bodies;
physical line breaks do not determine the block boundaries. Comments and
whitespace do not affect matching.

For example, a repeated sequence of `const t0 = data[0]`, `const t1 = t0 + 1`,
and `out[0] = t1` can match the next copy using `t2`, `t3`, and `out[1]`.
The whole three-statement sequence is reported together. Numeric literals and
digit runs in identifiers must each remain fixed or advance by a consistent
stride across copies. Decimal, hexadecimal, and octal integer spellings compare
by value. Operators, types, string contents, and the remaining identifier text
stay exact. Signed bit patterns, unknown bits, scaled literals, and integers
outside the detector's bounded arithmetic range stay exact as well.

Findings include the full source range, the first-copy range, statements per
copy, repetition count, an illustrative template, and the observed progressions.
`{p0}`, `{p1}`, etc. in a template are descriptive placeholders, not Pyrope
syntax. A progression such as `p0 = 4 + (2 * i)` uses a zero-based copy index.
Templates are limited to 1200 bytes and explanations to eight progressions;
the first-copy location always points to the full source.

Suggestions are ranked by estimated removable syntax tokens. Overlapping
suggestions from the same detector are suppressed; different rules may report
the same region. Ties use block size and stable source ordering. Numeric progression earns
the `likely-unrolled-loop` rule ID; exact repetition uses `repeated-code`.
These are heuristic suggestions, not proof of the original Verilog's structure
or of a replacement loop's equivalence. Numbered scalar names may need to become
an indexed collection before refactoring. The command never rewrites source.

| Option | Default | Meaning |
| --- | --- | --- |
| `--min-repeats N` | 7 | Repetition only: minimum complete copies; at least three are needed to check a stride |
| `--max-block-statements N` | 128 | Largest number of sibling statements in one copy; maximum 4096 |
| `--max-findings N` | 20 | Highest-ranked findings across all rules shown per file |

The additional rules are enabled by default and do not use `--min-repeats`:

- **`whole-tuple-copy`** finds at least two consecutive plain assignments with
  matching field paths, such as `dst.valid = src.valid; dst.data = src.data`.
  Nested fields identify the deepest shared bundle prefixes. The suggestion is
  to consider `dst = src` after checking complete field coverage, compatible
  types, and assignment semantics, then LEC it against the expanded copy.
  Indexed accesses, same-root copies, duplicate/overlapping fields, conversions,
  declarations, and modified assignments are excluded. Intervening statements
  break the sequence; comments do not.
- **`flattened-bundle-arguments`** groups escaped dotted call arguments such as
  ``child(`io_in.control.enable`=enabled, `io_in.data`=data)`` by their first
  path component. Even one such argument can suggest a structured bundle.
  Update the callee interface and its callers together; the analyzer does not
  resolve that interface. Ordinary dotted arguments, underscore-only names,
  and already structured tuples are not flagged. Each group is anchored on its
  first argument, with related locations identifying its contributing arguments.
- **`single-destination-conditional`** finds standalone exhaustive
  `if`/`elif`/`else` statements whose branches each contain one plain assignment
  to the same identifier or static field path. It suggests an `if` expression
  preserving branch order. Missing `else`, `unique if`, initializer clauses,
  declarations, branch-local calculations, calls in branch values, indexed
  destinations, compound assignments, and `wrap`/`sat` are excluded.
  A standalone `match` whose arms (and optional `else`) each contain one such
  assignment is flagged the same way, suggesting `dst = match ... { == V { value } ... }`.
  A `match` is always exhaustive, so it needs no `else`; unlike `if`, its arms may
  carry a `wrap`/`sat` modifier as long as every arm uses the same one
  (`wrap r = match ...`).

- **`hardcoded-reset`** finds an `if PORT { reg = CONST }` clear (the clearing arm
  may also be the `else`; `PORT`, `!PORT`, `not PORT`, `~PORT`, `PORT == 0|1` and
  `PORT != 0|1` are recognized) where `PORT` is a `U1`/`Bool`/`Reset` module input
  that is used **only** as a reset: every use is such a condition or a
  `reset_pin=PORT`. The test is functional, not by name. The suggestion is to
  declare the register `reg r:T:[reset_pin=PORT] = CONST` (adding `negreset=true`
  for an active-low condition) and delete the clearing arm. Excluded: a port also
  used as data, a later write to the register (the clear is then not a priority
  reset), both arms constant (a data mux), clears mixed with non-register
  assignments, `elif` chains that reuse the port, arrays/memories and latches.
- **`reset-port-type`** finds a `U1`/`Bool` input that only acts as a reset (same
  functional test, counting `reset_pin=PORT` uses too) and suggests declaring it
  `Reset`, since clocks and resets bind by type, not by name. It is the follow-up
  to `hardcoded-reset`: `struct_top_port.prp` (hand-written clear) triggers both,
  `struct_top_port_1.prp` (`reset_pin`, `U1`) only this one, and
  `struct_top_port_2.prp` (`Reset`) neither.

- **`narrow-scoped-mut`** finds a local `mut` initialized with a literal, assigned
  once in a narrower scope, and read only after that assignment within the same
  scope and lambda. It suggests deleting the outer declaration and declaring a
  `const` at the assignment, retaining an explicit unsigned type. Early/outside
  reads, additional or partial writes, ref escapes, captures, modifiers, custom
  types, and attributes are excluded.
- **`compact-bit-packing`** finds a local `mut` followed by consecutive bit writes
  covering its entire value from bit zero, with each source's known unsigned
  width matching its destination window. It suggests `const dst:U<N> = array#[..]`
  for all lanes of one fixed-size array in order, otherwise
  `const dst:U<N> = (low_lane, ..., high_lane)#[..]`. Explicit destination types are
  retained; untyped destinations get the total packed width (`U<N>`), as required
  by the compiler. Gaps, overlaps, retained high bits, unknown/signed widths, dynamic
  slices, side effects, intermediate reads and later writes/ref escapes are
  excluded. Both new rules skip files with parse errors and ambiguous bindings;
  missed suggestions are preferred to uncertain rewrites.

New findings include `score` plus `destination`/`source`/`field_count`,
`bundle`/`argument_count`, `destination`/`branch_count`, `reset`/`registers`/`polarity`/`value`/`port_type`, or `port`/`declared_type`/`reset_uses`, respectively.
The new rules additionally expose `variable`/`assignment_line` or
`destination`/`lane_count`/`bits`/`packing`.
Related locations are capped at eight per finding; counts include all matches.
Existing repetition findings retain their template/count/progression attributes.

### Silencing a suggestion on purpose

Some code is deliberately "not so nice" (tests, generated fixtures). A comment
`// prp-style-allow code-a, code-b` (or the `/* ... */` form) silences those
rule codes from the comment to the end of the **enclosing scope**; when the tree
traversal leaves that scope the allow ends. Placed first in a function body it
covers the whole function; inside an `if` arm only that arm; at file level the
rest of the file. Only findings that start after the comment are silenced.
Codes are the `code` of the diagnostic (`single-destination-conditional`,
`repeated-code`, ...); unknown codes are ignored. The summary `suppressed`
attribute counts the silenced findings.

```pyrope
pub comb fun3(a:U3, b:U7, cond:Bool) -> (res:U4) {
  // prp-style-allow single-destination-conditional
  if cond { res = a + 1 } else { res = b#[0..<4] }
}
```

Findings and, when a file has any, a per-file summary use the normal diagnostic stream: human text on
stderr with `--diag-fmt pretty`, JSONL with `--diag-fmt json`, and a structured
file with `--emit diagnostics:PATH`. JSON records include spans, related notes,
and rule-specific `attrs`. `-q` suppresses stderr while
preserving the declared diagnostics file. There is no stdout result envelope.

Syntax errors produce a `partial-analysis` warning; intact sequences are still
checked, and error-containing statements break candidate sequences. Exit status:
0 when no file reports a suggestion (a partial parse alone stays 0), 2 when any
suggestion is reported, and 1 for input or parser infrastructure failures (1 wins
over 2); later files are still checked. An invalid invocation is a usage error,
which also exits 2. A script can test `$? -ne 0`.

The repetition detector does not match reordered or scattered statements, arbitrary
identifier renamings, or irregular iteration progressions. It does not merge
sequences across scope boundaries. The source-only analyzer in
`inou/prp/pyrope_style.hpp` returns a report independently of CLI presentation, so future
rules and editor integration can share it.

The analyzer lives in `inou/prp/pyrope_style.{hpp,cpp}`; one small fixture per rule
(plus negatives and the allow-scope cases) is in `inou/prp/tests/style/` and runs as
`bazel test //inou/prp:prp-style-<name>` (see its README).

Run the focused regressions with:

```sh
bazel test -c opt //lhd/tests:lhd_style_test //lhd/tests:lhd_fmt_test //lhd/tests:lhd_help_test
```
