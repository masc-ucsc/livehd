# lhd tooling

`lhd` is the LiveHD command line: compile, simulate, prove, synthesize, time
and inspect Pyrope and Verilog. Build it in a LiveHD checkout with
`bazel build -c opt //lhd:lhd` (binary at `bazel-bin/lhd/lhd`). Prefer the
checkout binary over a copied one: `lhd sim` needs its runtime headers, and a
copy goes stale. Use `-c opt` for anything performance related; a `dbg` build
is several times slower.

## Contract

* Stateless and deterministic. Exit code 0 is a pass. The last stdout line is
  one JSON result object; on failure `error.class` says why (`usage`, `syntax`,
  `equiv_fail`, `unsupported`, `internal`, `config`, ...).
* `--result-json PATH` writes that object to a file; scripts should read it
  there rather than parse stdout.
* Diagnostics: `--diag-fmt auto|jsonl|pretty` (auto is pretty on a terminal,
  JSONL when piped). Pass `--diag-fmt pretty` when piping through grep/head;
  `--emit diagnostics:PATH` saves them for a tool.
* Every command describes itself: `lhd help <cmd>` (== `lhd <cmd> --help`)
  prints the accepted arguments as JSON. `lhd list options|steps|emit-kinds|error-classes`
  and `lhd describe <name>` (an option such as `synth.adder`, or a verb such as
  `'tool sta'`) give the vocabulary. Check them before guessing a flag.
* Options are `--set namespace.key=value` (`synth.*`, `pass.abc.*`,
  `formal.*`, `sim.*`, `compile.*`). A mistyped key or value is a usage error,
  never silently ignored.
* `--workdir W` keeps intermediate artifacts and enables incremental reuse;
  `--set lhd.incremental=false` forces a cold run. Put workdirs on a disk with
  room (large designs produce many files).

## Artifacts

* Inputs: `.prp`, `.v`/`.sv` (bare paths infer the kind), or IR directories
  `ln:DIR` (LNAST) and `lg:DIR` (LGraph). IR directories are valid inputs to
  compile, sim, lec, synth and tool.
* Outputs: `--emit verilog:PATH`, `--emit-dir lg:DIR`, `--emit-dir ln:DIR`,
  `--emit-dir pyrope:DIR` (Verilog or Pyrope to Pyrope; not from `lg:`, there
  is no graph decompiler), `--emit-dir report:DIR`.
* Generated module names are `file.entity` (`cnt.counter`). A `--top` that is
  not found falls back to a unique entity match with a warning; pass the full
  `file.entity` name to tools that work on `lg:` directories.

## Commands

```sh
lhd compile foo.prp                               # parse + lower + diagnostics
lhd compile foo.prp --top foo --emit verilog:foo.v --workdir W
lhd compile foo.prp --emit-dir lg:net --set compile.upass.inline=false   # keep hierarchy
lhd sim foo.prp                                   # every test block
lhd sim foo.prp foo.basic --arg n=4               # one test, runtime arguments
lhd formal verify foo.prp props.verify.prp --top foo --set formal.bound=12
lhd lec --ref gold.v --impl foo.prp --top foo     # sequential equivalence
lhd synth foo.prp --top foo --workdir W --stats   # compile -> color -> map -> STA
lhd pass opentimer lg:net cells.lib design.sdc --top foo.foo --set pass.opentimer.critical=true
lhd tool sta paths=5 'slack<0' lg:net --top foo.foo
lhd pyrope fmt -i foo.prp                         # formatter
lhd pyrope style foo.prp --diag-fmt pretty        # repetition finder
lhd tool cat|grep|diff|tree ...                   # inspect ln:/lg: artifacts
```

### sim

Builds a C++ simulation of the `test` blocks. Useful: `--list-tests`,
`--seed N`, `--set sim.vcd=true`, `--probe SIG`, `--break-when 'SIG OP VALUE'`,
`--vcd-on-fail`. Only asserts inside `test` blocks are checked at simulation
time; design-body asserts belong to `pass.formal`.

### formal verify

Proves design `assert`/`assume` and `formal` blocks by BMC from reset, with
per-obligation verdicts (PROVEN inductive or bounded, REFUTED with a replay
trace under `--workdir`). `formal.strict` is on: an inconclusive run exits
nonzero. Select blocks with `--formal '<glob>'`.

### lec

* Sides are `verilog:`/`pyrope:`/`ln:`/`lg:`; `--ref` is the reference,
  `--impl` the implementation. Solver `--set formal.solver=cvc5|bitwuzla|lgyosys`;
  budget `--set formal.timeout=SECONDS`.
* Read `lec.verdict` with `lec.bounded` and `lec.bound`: `proven` with
  `bounded=true` holds only to that depth. `refuted` is a real difference
  (find which side is wrong). `unknown` is neither: back it with simulation.
  Setup errors and early inconclusive results are not timeouts.
* Refactor in small steps and prove each against the previous verified
  snapshot; small deltas prove in seconds where whole-design proofs time out.
  Compile both sides with `--set compile.upass.inline=false --emit-dir lg:DIR`.
* Renamed instances break automatic state pairing. Pair them explicitly with
  `--set formal.lec.match='old_inst.q=new_inst.q'` (equal widths only).
  Packed or split state cannot be paired; expect `unknown` and lean on
  simulation plus a proven intermediate step.
* Backends can disagree (graph-level solvers vs the emitted-Verilog path).
  When a verdict surprises you, get an independent oracle: emit Verilog and
  compare against the golden with a Yosys-based equivalence check or a random
  simulation sweep. Two LiveHD-emitted sides can share one lowering bug.

### synth

`lhd synth` is the one-shot flow: compile, `pass color synth` (partition into
regions), a mapper per region (`--set synth.mapper=abc` default, or `usyn`),
then OpenTimer STA, over one Liberty (`--set synth.liberty=cells.lib`).

* `--stats` prints per-region rows; the result JSON carries `qor.abc.total`
  (gates, area), per-region data, `qor.sta` (max delay, critical pin and
  source), `qor.timing_target` (target vs achieved), and the memory table.
* The timing target is `--set synth.delay=PS` (or `pass.abc.delay`) or an SDC
  clock. An unreachable target is reported, not hidden.
* Memories: `--set pass.abc.memory=false` keeps every memory as a black box,
  which is what you want when comparing logic QoR (and what a macro flow does).
* Emit the netlist with `--emit-dir lg:DIR` (for STA, LEC, tool queries) or
  `--emit verilog:PATH`.
* Source attributes and run-wide options that steer the result are in
  `synth.md`; `lhd describe synth.<key>` documents each.

### Critical paths (opentimer + tool sta)

```sh
lhd pass opentimer lg:net cells.lib design.sdc --top top.top --set pass.opentimer.critical=true
lhd tool sta lg:net --top top.top                       # critical cells, worst slack first
lhd tool sta lg:net --top top.top clock=core 'slack<0'  # filters are AND-combined
lhd tool sta paths=5 lg:net --top top.top               # 5 worst equal-slack groups
```

* The pass annotates every node of the netlist with its slack per clock and
  marks the ones within `pass.opentimer.critical_percent` (default 5 %) of the
  period. Clocks come from the SDC (`create_clock ... [get_ports clk]`) or
  `--set pass.opentimer.targets=clk:1ns`. Choose a period the design misses so
  `slack<0` selects something.
* `paths=N` folds cells of equal slack into groups and prints, per group:
  slack, cell count, launching and capturing state, and the source lines the
  cells carry. Parallel bit-slices of one structure fold into one group.
* Mapped cells often carry only coarse provenance (a register declaration
  line, or none). Use the start and end state names to identify the logic,
  then read the source between them.
* Annotations are stored in the `lg:` directory and checked against the graph:
  re-run the pass after any change to the netlist.

### pyrope fmt / style

`lhd pyrope fmt -i` formats in place (one statement per line, sorted named
lists, `f(x=x)` becomes `f(x)`). Prove the formatted file against its
pre-format snapshot; do not assume the formatter is semantics-free.
`lhd pyrope style` reports repeated statement blocks and flattened bundle
arguments (exit 2 = suggestions, 1 = input failure; exit 0 can still hide a
partial parse, so read the diagnostics).

### tool

`lhd tool cat|grep|tree|diff` inspect `ln:`/`lg:` content with field filters
(`kind=sub`, `color=3`, `src=alu.prp`, `bits>8`); `--top` restricts to a
module and its children; `--max N` caps rows (0 = all).

## Triage

Read diagnostics by `category`:

* `syntax`/`name`/`type`/`bitwidth`: the source is wrong; fix it.
* `unsupported`: valid Pyrope the compiler cannot lower yet; rewrite around it
  (see the not-implemented list in `semantics.md`), do not "fix" correct code.
* `internal`: a compiler bug; reduce it to a small reproducer instead of
  changing the source. Exception: landing-cycle ("lands at cycle(s) ... but ...
  is declared"), combinational-loop and clock/reset ambiguity errors can print
  as `internal` although they are source errors.
* A crash or abort is always a tool bug. A timeout is a budget question; a
  refutation is a correctness question.

Passing `lhd compile` is necessary, not sufficient: the frontend accepts some
stale forms. Follow `semantics.md` for style and meaning.

Checking a comptime-only file (no hardware entity) needs
`--set upass.tolg=false --set upass.verifier=true`; a plain compile forces
lowering and reports spurious errors on such files.
