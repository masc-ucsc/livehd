# LiveHD CLI reference

`lhd` compiles, simulates, proves, synthesizes, times and inspects Pyrope and
Verilog. Build it in a checkout with `bazel build -c opt //lhd:lhd` (binary at
`bazel-bin/lhd/lhd`). Use the checkout binary, not a copy: `lhd sim` needs the
runtime headers and a copy goes stale. Use `-c opt` for anything
performance-related; `dbg` is several times slower.

## Contract

- For native compile, simulation, proof and synthesis commands, exit 0 is a pass. The last stdout line is a JSON result
  object; on failure `error.class` says why (`usage`, `syntax`, `equiv_fail`,
  `unsupported`, `internal`, `config`, `missing_file`...). `--result-json FILE`
  writes it to a file: scripts should read that, not parse stdout.
- Diagnostics: `--diag-fmt auto|jsonl|pretty` (auto is JSONL when piped, so
  pass `pretty` before grep/head). `--emit diagnostics:FILE` saves them.
- Self-describing: `lhd help <cmd>` prints accepted arguments as JSON;
  `lhd list options|steps|emit-kinds|error-classes` and `lhd describe <name>`
  (an option like `synth.adder`, or a verb like `'tool sta'`) give the
  vocabulary. Check them before guessing a flag.
- Options are `--set namespace.key=value` (`synth.*`, `pass.*`, `formal.*`,
  `sim.*`, `compile.*`, `lhd.*`). A mistyped key or value is a usage error.
- `--workdir W` keeps intermediates and enables incremental reuse;
  `--set lhd.incremental=false` forces a cold run (say which you timed). Put
  workdirs on a disk with room.

## Artifacts

- Inputs: `.prp`, `.v`/`.sv` (bare paths infer the kind), or IR directories
  `ln:DIR` (LNAST) and `lg:DIR` (LGraph), accepted by compile, sim, lec, synth
  and tool.
- Outputs: `--emit verilog:FILE`, `--emit-dir lg:DIR | ln:DIR | pyrope:DIR |
  report:DIR` (Pyrope output is from source only; there is no graph
  decompiler).
- Generated module names are `file.entity`. An unmatched `--top` falls back to
  a unique entity match with a warning; on `lg:` directories pass the full
  qualified name.
- `lhd tool cat|grep|tree|diff` inspect `ln:`/`lg:` content with field filters
  (`kind=`, `color=`, `src=`, `bits>8`); `--top` restricts to a module and its
  children, `--max N` caps rows (0 = all).

## Commands

```sh
lhd compile foo.prp --top foo --emit verilog:foo.v --workdir W
lhd compile foo.prp --emit-dir lg:net --set compile.upass.inline=false  # keep hierarchy
lhd sim foo.prp [foo.test_name] [--arg n=4]
lhd formal verify foo.prp --top foo --set formal.bound=12
lhd lec --ref gold.v --impl foo.prp --top foo
lhd synth foo.prp --top foo --workdir W --stats
lhd pass opentimer lg:net cells.lib design.sdc --top foo.foo --set pass.opentimer.critical=true
lhd tool sta paths=5 'slack<0' lg:net --top foo.foo
lhd pyrope fmt -i foo.prp ; lhd pyrope style foo.prp --diag-fmt pretty
```

A comptime-only file (no hardware entity) needs
`--set upass.tolg=false --set upass.verifier=true`; a plain compile forces
lowering and reports spurious errors.

## sim

Builds and runs a C++ simulation of the `test` blocks. Useful: `--list-tests`,
`--seed N`, `--set sim.vcd=true`, `--probe SIG`, `--break-when 'SIG OP VALUE'`,
`--vcd-on-fail`. Only asserts inside `test` blocks are checked at run time;
design-body asserts belong to formal. Default backend is LLVM
(`sim.tune.backend=llvm`); `slop` is the reference C++ backend, useful to
cross-check a suspected simulator bug. Some runs (VCD, probes, queries) fall
back to the older generator automatically.

`lhd sim` writes sources under `<workdir>/sim` and builds `drv.bin` itself
(parallel, incremental). That directory records its command: inside it, plain
`lhd sim` re-reads the sources, regenerates what changed, builds and runs;
`--setup-only` only regenerates; `--run-only` builds and runs the existing
tree; `--set sim.compile_only=true` stops after the build. Extra arguments are
appended to the recorded command and win.

A memory/array address must be provably inside `[0, size)` (mask or bound the
index) or the compile error `mem-address-overflow` appears;
`--set sim.debug=true` checks unproven addresses at run time instead.

## formal verify

BMC from reset of `assert`/`assume`/`formal` blocks with per-obligation
verdicts (proven inductive or bounded, refuted with a replay trace under
`--workdir`). Strict mode makes an inconclusive run exit nonzero. Select
blocks with `--formal '<glob>'`.

## lec

- `--ref` is the reference, `--impl` the implementation; each is
  `verilog:`/`pyrope:`/`ln:`/`lg:`. Solver `--set formal.solver=...`, budget
  `--set formal.timeout=SECONDS`.
- Read `lec.verdict` with `lec.bounded` and `lec.bound`: `proven` bounded holds
  only to that depth; `refuted` is a real difference (find which side is
  wrong); `unknown`/timeout leaves the claim open, so back it with simulation.
- Refactor in small steps, proving each against the previous verified
  snapshot; small deltas prove quickly where whole-design proofs time out.
  Compile both sides with `--set compile.upass.inline=false --emit-dir lg:DIR`.
- Renamed state breaks automatic pairing: use
  `--set formal.lec.match='old.q=new.q'` (equal widths, reference on the
  left). Packed or split state cannot be paired.
- Backends can disagree (graph-level vs emitted-Verilog path), and two
  LiveHD-emitted sides can share one lowering bug. When a verdict surprises
  you, get an independent oracle: emit Verilog and use an external equivalence
  check or random simulation against the original.

## Synthesis and timing

`lhd synth` is the one-shot flow: compile, partition into regions, map each
region (`--set synth.mapper=abc|usyn`), then STA over a Liberty
(`--set synth.liberty=...`).

- `--stats` prints per-region rows; the result JSON carries total gates and
  area, per-region data, STA max delay with critical pin/source, target vs
  achieved timing, and the memory table.
- Target: `--set synth.delay=PS` or an SDC clock. An unreachable target is
  reported, not hidden; pick one the design misses when hunting paths.
- `--set synth.memory=macro` keeps memories as black boxes (use when
  comparing logic QoR or for macro flows). Confirm expected SRAMs survived
  mapping before placing them.
- Keep mapper, Liberty, clock targets and memory realization fixed when
  comparing runs. Record cells, area and delay together.
- `W/synth/manifest.json` records the command and result. A bare `lhd synth`
  inside `W/synth` replays it: unchanged inputs mean no work and the stored
  report; any change re-runs incrementally. `--emit-dir lg:DIR` yields a
  netlist for STA, LEC and tool queries.

### Pre-CTS chunk placement

Start from the design's existing RTL, top, Liberty, LEFs, constraints and floorplan settings. `init` saves that recipe and exports one worker packet per physical chunk. The budget sizes chunks and applies to isolated workers. The producer still loads the flat design. By default, the planner favors colors or macros sharing two-object nets and breaks ties in hierarchy order. To pack in hierarchy order, add `--set synth.place.chunk.connectivity=false` to `init`. Use separate workdirs to compare budgets or packing choices.

```sh
lhd synth chunk init design.v --top top --workdir W --budget 1GiB \
  --set synth.liberty=cells.lib --set synth.lef=tech.lef,cells.lef
lhd synth chunk list --workdir W --json
```

Use an ID from `list` as `id` below. `show` returns neighboring IDs, ports with their `net_index` and die coordinates in DBU, and stage results.

```sh
lhd synth chunk show "$id" --workdir W
lhd synth chunk list "$id" --neighbors --workdir W
lhd synth chunk macro "$id" --neighbors --workdir W
lhd synth chunk final "$id" --neighbors --workdir W
```

One ID runs one chunk. Several IDs run that set. `--neighbors` adds one hop of edge-sharing or overlapping homes. Explicit IDs rerun the selected jobs. Omitting IDs runs outstanding jobs. Final fixes the macro positions from a current macro result. Without that handoff it places the memories and cells together. Macro placement can slide a shared contact along the common chunk edge and publishes its coordinates for all incident chunks. This workflow stops before CTS. Read `list --json` for per-stage status, positions, reports and worker peak RSS.

After a source edit, `lhd synth chunk update --workdir W` replays the recipe and accepted plan guidance. Read the delta for added, removed, changed and reusable chunks. Use `--budget 512MiB` on update to resize the plan. To accept a measured shared contact after macro placement, use the chosen port's `net_index` as `net`.

```sh
lhd synth chunk move-io "$id" "$net" --from-macro --workdir W
lhd synth chunk list --stage final --status stale --workdir W
lhd synth chunk macro --workdir W
lhd synth chunk final --workdir W
```

A contact change marks completed work stale on all incident chunks, including the source. A macro rerun also invalidates its previous final. Use `chunk gui --workdir W` to run selected workers and inspect placement feedback. To change the plan itself, use the [live floorplan editor](agent_socket.md).

For pre-CTS QoR comparisons, score the same connected full design with the same clocks, libraries and RC. Request `--set synth.place.snapshots=true` at init to obtain `synth/place/chunk-plan.def`. Use `lhd synth chunk score --workdir W` for assembled physical-pin HPWL and estimated setup WNS. It reads compact worker summaries, including frozen neighbors and die pins. Local worker HPWL includes virtual boundary contacts. Use `lhd synth chunk assemble --workdir W` for the verified final position manifest. [Placement reference](../../../pass/place/README.md#color-and-macro-chunk-floorplan)

### Critical paths

```sh
lhd pass opentimer lg:net cells.lib design.sdc --top t.t --set pass.opentimer.critical=true
lhd tool sta lg:net --top t.t                        # worst slack first
lhd tool sta lg:net --top t.t clock=core 'slack<0'   # filters AND-combined
lhd tool sta paths=5 lg:net --top t.t                # 5 worst equal-slack groups
```

The pass annotates each node with slack per clock and marks those within
`pass.opentimer.critical_percent` of the period. Clocks come from the SDC or
`--set pass.opentimer.targets=clk:1ns`. `paths=N` folds equal-slack cells into
groups (slack, count, launching/capturing state, source lines). Mapped cells
often carry coarse provenance: use the start/end state names to locate the
logic, then read the source between them. Annotations live in the `lg:`
directory: re-run the pass after any netlist change.

## pyrope fmt / style

`fmt -i` formats in place; prove the result against its pre-format snapshot
rather than assuming it is semantics-free. `style` reports repeated statement
blocks and flattened bundle arguments (exit 2 = suggestions, 1 = input
failure; exit 0 can hide a partial parse, so read the diagnostics).

## Triage

- `syntax`/`name`/`type`/`bitwidth`: fix the source.
- `unsupported`: valid code not yet lowered; rewrite around it, don't "fix"
  correct code.
- `internal`: compiler bug; reduce to a minimal reproducer. Landing-cycle,
  combinational-loop and clock/reset ambiguity errors can print as `internal`
  though they are source errors.
- A crash is always a tool bug; a timeout is a budget question; a refutation is
  a correctness question.
