# Lgraph_IR_Semantics_Simulator

This directory holds the working set of LiveHD's IR as semantics simulator.

The idea of this direction is this. LiveHD compiles RTL into its own graph, the
LGraph. The `pass.lean` pass then writes that graph out as a piece of data
called a `DesignCert`, short for design certificate. A small Lean program reads
that data and runs it. The certificate is therefore not a description of the
design that something else has to interpret. It *is* the design's meaning,
because the Lean program that runs it is the definition of what it means.

**The simulator itself is not in this directory.** It is Lean source, and it
lives in `formal/lean/LeanSemanticPrimitives/Compiler/`. This directory is
named for the work it serves, not for what it contains. What is here is the set
of designs we measure, the measurements, and the scripts and tests that produce
them. The next section says where the code is.

```text
pass/lean/Lgraph_IR_Semantics_Simulator/
  manifests/   which designs we run, and the parameters needed to elaborate
               them (MODULE_PARAMS.tsv, coreet_*.tsv, cva6_*.tsv, PROVENANCE)
  evidence/    what was measured: the censuses, the sweeps, the ledger of
               blocked modules, and write-ups of individual findings
  scripts/     the tooling: the CORE-ET flow (run_coreet_module_lean.sh and
               the coreet_* helpers), the coverage ledger, the lgraph-sim
               launcher generator, the direct sweep, and the
               RTL-versus-regenerated-Verilog differential
  tests/       this simulator's tests, including the D4 runtime sweep in
               tests/d4/ (its corpus lists, its run records and its gates)
```

Throughout this file, a bare `evidence/`, `manifests/`, `scripts/` or `tests/`
path is relative to this directory. Anything else is given from the top of the
repository.

## Code layout

This simulator's code lives in three places, and other parts of LiveHD use
two of them.

| what | where | who else uses it |
|---|---|---|
| the simulator and its proofs, in Lean | `formal/lean/LeanSemanticPrimitives/Compiler/` | Translation Validation and the Futamura projection check their results against its `interpretDesign` |
| the front half of the exporter, in C++ | `pass/lean/Exporter/Frontend/` | every route, including legacy |
| the `DesignCert` writers, in C++ | `pass/lean/Exporter/DesignCert/` | Translation Validation and the Futamura projection read what they write |
| this simulator's corpus, measurements, scripts and tests | this directory | no other route |

The Lean side runs a certificate:

```text
formal/lean/LeanSemanticPrimitives/Compiler/
  DesignCert.lean        the certificate's datatype
  DesignSemantics.lean   interpretDesign, the reference semantics
  DirectSemantics.lean   directStep, the checked interpreter, one cycle
  DirectTrace.lean       runDirect, the checked interpreter, a whole run
  CompileDesign*.lean    compileDesign, the compiled fast path, and its proof
  DirectVsCompiled.lean  the theorems that make the three agree
  CertIO*.lean           the proved reader for .dcert files
```

The C++ side writes the certificate. It is the exporter inside `pass.lean`:

```text
pass/lean/Exporter/Frontend/
  pass_lean.*                 the pass entry point; picks legacy or verified_compiler
  lean_options.*              the pass options
  graph_access.*              reads the LGraph
  design_scan.*               walks the graph once and copies what the certificate
                              needs into plain values
  certificate_ir.*            the certificate's in-memory form
  certificate_builder.hpp     helpers for building that form
  memory_lowering.*           lowers each memory, read side and write side
  design_io_meta.*            writes <Top>_io.json, the port and clock sidecar
  lean_format.*               output helpers shared by every writer
  scripts/                    oracles that run a certificate against the original RTL
  tests/                      exporter test tools and integration records

pass/lean/Exporter/DesignCert/
  emit_design_cert.*          writes <Top>_designCert, the Lean source form
  emit_dcert.*                writes <Top>.dcert, the compact DCERT2 form
  scripts/                    checks on the DesignCert format: vc_gates.py,
                              dcert_parity.py, cert_shape_check.py, cert_cut.py
```

The exporter's Bazel targets stay in `pass/lean/BUILD` under their old labels.
`pass/lean/README.md` documents the exporter in more detail.

## How the simulator works

The chain has four steps. Each one is listed with the code that implements it.

```
RTL (.sv)
  │ 1. the yosys-slang reader                 inou/yosys
  ▼
LGraph
  │ 2. edge normalisation                     pass/single_edge
  ▼
LGraph, normalised
  │ 3. the exporter                           pass/lean/Exporter/
  ▼
DesignCert
  │ 4. the simulator                          formal/lean/LeanSemanticPrimitives/Compiler/
  ▼
the design's behaviour
```

Step 3 writes two files for each design, `<Top>_Lgraph.lean` and
`<Top>_io.json`. In `verified_compiler` mode it also writes `<Top>.dcert`,
which is the same certificate in a compact binary form.

A `DesignCert` is a record with six fields: `sources`, `nodes`, `outputs`,
`flops`, `memories` and `clocks`.

Step 4 is the part that is proved. There are three ways to run a certificate,
and they are three separate programs:

| program | file | what it is for |
|---|---|---|
| `interpretDesign` | `DesignSemantics.lean` | the reference. It defines what a certificate means. It is not written to be fast. |
| `directStep`, `runDirect` | `DirectSemantics.lean`, `DirectTrace.lean` | the checked interpreter. It validates the certificate first and may refuse it. |
| `compileDesign`, `compileAndRun` | `CompileDesign*.lean` | the fast path. It turns the certificate into a straight-line program once, then runs that. |

The three agree, and that is a theorem rather than a test:

* `directStep_correct` — if the checked interpreter answers, its answer is what
  `interpretDesign` says.
* `compileDesign_correct` — the compiled program equals `interpretDesign` for
  every clock pattern, every input and every state.
* `directStep_eq_compileAndRun` — the two runnable programs agree with each
  other.
* `compiledTrace_eq_refTrace`, `runDirect_eq_compiledTrace` — the same
  agreement over a whole run, not just one cycle.

These proofs use no axioms beyond Lean's own, and contain no `sorry`. A `sorry`
is Lean's placeholder for an unfinished proof, so having none means nothing is
left unproved inside that step.

`CertIO.lean` is the reader for the `.dcert` file format, and it is itself
proved correct, so reading a certificate back in does not add anything that has
to be believed.

## How to build it

Two separate builds, because two languages are involved.

**The exporter** is C++ and builds with Bazel:

```bash
bazel build -c opt //lhd:lhd
```

On a host whose default compiler is newer than the one the tree expects, add
`--repo_env=CC=<gcc> --repo_env=CXX=<g++>` to pick an older one. Choose a Bazel
output directory on a local disk rather than on a network filesystem, with
`--output_user_root=<dir>`, or the build will be slow.

**The simulator** is Lean and builds with Lake. The Lean version is pinned in
`formal/lean/lean-toolchain`. The library depends on Mathlib, so fetch the
prebuilt object files once:

```bash
cd formal/lean
lake exe cache get      # once, downloads Mathlib's .olean files
lake build              # the LeanSemanticPrimitives library and its proofs
lake build LgraphSim    # the standalone simulator binary, named lgraph-sim
```

`LgraphSim` is deliberately not built by `lake build` on its own. Building a
binary on every proof run would slow the proof work down for no benefit.

## How to run it

### A simple example

`txfma_adder` is a small adder from the CORE-ET benchmark. It is a good first
run because the whole flow finishes in about a second.

```bash
COREET_ROOT=<path to core-et> \
COREET_TOP=txfma_adder \
LEAN_MODE=verified_compiler \
bash pass/lean/Lgraph_IR_Semantics_Simulator/scripts/run_coreet_module_lean.sh
```

That script does the whole chain: it reads the RTL, normalises the graph,
exports the certificate, and checks it. The run prints:

```
compile exit=0
single_edge exit=0  pass.single_edge on `txfma_adder`: skipped: no latch, no negedge state, one clock net
lean emit exit=0
static gates: gate_status=0
sources=4 nodes=2
sorry=0
PASS: all verified_compiler static gates
```

The design came out as a certificate with 4 sources and 2 nodes. `sorry=0`
means the emitted Lean file left nothing unproved.

To execute the certificate rather than only check it, generate a launcher and
run the simulator binary:

```bash
python3 pass/lean/Lgraph_IR_Semantics_Simulator/scripts/make_sim_launcher.py <path to txfma_adder_Lgraph.lean>
cd formal/lean && lake build LgraphSim
./.lake/build/bin/lgraph-sim txfma_adder --cycles 10
```

The simulator's options are:

```
usage: lgraph-sim DESIGN [options]
  --list                 list the certificates this binary carries
  --inputs FILE          input trace, one cycle per line (default: all zeros);
                         a line may start with @<bits>, one 0/1 per declared clock
                         (ordinal 0 first) = which clocks fire that cycle (default: all)
  --state FILE           initial state (default: all zeros)
  --cycles N             cycles to run (default: the input trace's length, else 1)
  --show-state           also print the flop state each cycle
  --watch-mem IDX:ADDR   print memory IDX at ADDR each cycle (repeatable)
  --check-only           run the certificate and runtime checks, then exit
  --hex                  print values in hexadecimal
exit status: 0 ok, 1 refused certificate or runtime shape, 2 usage, 3 I/O
```

### A complicated example

`cva6_hpdcache_subsystem` is the data cache subsystem of the CVA6 RISC-V core.
It is the largest design this simulator has run. It is interesting because it
has memories in it, which is the hardest part of the certificate format.

Measured in the Direction 4 sweep, recorded in
`pass/lean/Lgraph_IR_Semantics_Simulator/tests/d4/cva6_30_rows.tsv`:

| | |
|---|---|
| sources | 97,741 |
| nodes | 108,612 |
| flops | 542 |
| memories | 12 |
| time to parse the certificate | 5,129 ms |
| time to compile it and run one cycle | 10,351 ms |

The same flow runs it, with the CVA6 corpus instead of CORE-ET. The run is
driven by `pass/lean/Lgraph_IR_Semantics_Simulator/tests/d4` rather than by the CORE-ET script, because CVA6
modules need generated wrappers before they can be elaborated on their own.

## Performance

These numbers come from the recorded sweeps named in each row. The column
"one cycle" is the time for `runCheckedAllEdges`, which compiles the
certificate and then executes a single cycle with every clock firing. The
compile is repeated on each call, so for a long run the per-cycle cost is much
lower than this figure.

| design | corpus | sources | nodes | flops | memories | parse | one cycle |
|---|---|---|---:|---:|---:|---:|---:|
| `txfma_align_shf` | CORE-ET | 4 | 3 | 0 | 0 | 1 ms | 1 ms |
| `ariane_regfile` | CVA6 | 1,034 | 1,121 | 32 | 0 | 82 ms | 21 ms |
| `minion_dcache_tensor_load` | CORE-ET | 9,349 | 11,424 | 112 | 0 | 516 ms | 721 ms |
| `vpu_mask` | CORE-ET | 14,590 | 14,860 | 60 | 0 | 701 ms | 2,026 ms |
| `csr_regfile` | CVA6 | 32,853 | 34,897 | 136 | 0 | 1,744 ms | 46,623 ms |
| `cva6_hpdcache_subsystem` | CVA6 | 97,741 | 108,612 | 542 | 12 | 5,129 ms | 10,351 ms |

Parsing scales smoothly with the size of the certificate. Execution does not:
`csr_regfile` is a third of the size of the cache subsystem and takes four
times as long for one cycle. Execution cost follows the shape of the logic, not
the node count.

## Benchmarks covered

**CORE-ET**, a many-core design with vector units. The corpus is 122 modules,
chosen as the minion-core slice of the design; `manifests/` records exactly
which, and how they were chosen.

* 109 of 122 export a certificate on the default pipeline
  (`evidence/CENSUS_MASTER_122.tsv`).
* 112 of 122 export when the optimiser is set back to the previous engine with
  `--set cprop.engine=classic`. The difference is an upstream regression
  written up in `evidence/AEGRAPH_EXPORT_REGRESSION.md`.
* 93 of 122 were parsed and executed in the Direction 4 runtime sweep
  (`pass/lean/Lgraph_IR_Semantics_Simulator/tests/d4/coreet_122_r2_rows.tsv`).

**CVA6**, a 64-bit RISC-V application-class core.

* 124 modules elaborate (`evidence/SWEEP_master_cva6_elab.tsv`).
* 110 of them export a certificate (`evidence/SWEEP_master_cva6_emit.tsv`).
* 87 were typechecked in Lean with their axiom use audited, and are recorded
  `PROVEN` (`evidence/SWEEP_master_cva6_prove.tsv`). That sweep took 2,236
  seconds in total, between 12.5 and 161 seconds per module.
* 30 were run through the Direction 4 runtime sweep, and all 30 executed
  (`pass/lean/Lgraph_IR_Semantics_Simulator/tests/d4/cva6_30_rows.tsv`).

Regenerate the coverage summary with:

```bash
pass/lean/Lgraph_IR_Semantics_Simulator/scripts/coverage_ledger.py \
  --base pass/lean/Lgraph_IR_Semantics_Simulator/evidence/CENSUS_MASTER_122.tsv
```

## The trust boundary

This is the most important thing to understand before quoting any result.

Step 4 of the chain is proved. The three ways of running a certificate agree
with each other, for every input and every state, with no axioms and no
`sorry`.

**Nothing proves that the certificate is the RTL.** That is steps 1 to 3, and
they are not covered by the proof. What checks them today:

| step | what checks it |
|---|---|
| 1 and 2, the reader and the normaliser | `lhd lec`, comparing the normalised graph against an independent second read of the same RTL. This is a real equivalence check, but it is **off by default** (`RUN_LEC_GATE=false`). |
| 3, the exporter | **No proof, and no check of its full behaviour across the corpus.** Narrower checks exist, listed below. |

What does check step 3 is narrow. None of it is formal.

* **Four RTL differentials**, in `pass/lean/Exporter/Frontend/scripts/`:
  `sign_oracle.sh`, `sdiv_oracle.sh`, `reset_expr_oracle.sh` and
  `setmask_oracle.sh`. Each runs iverilog on the original RTL and the
  certificate's `interpretDesign` side by side, cycle by cycle, for one kind of
  design shape. Each also has controls that must fail. They cross steps 1 to 3
  together, but only for those shapes.
* **Seven value gates** in the same folder: `be_oracle.sh`, `clock_oracle.sh`,
  `collide_oracle.sh`, `hotmux_oracle.sh`, `mem_oracle.sh`,
  `reset_oracle.sh` and `row_oracle.sh`. Each takes a certificate that the real
  exporter produced from a small hand-built graph, steps it in Lean, and
  compares the result with values written in the gate. They check step 3
  against a stated expectation, not against RTL.
* **`cert_lgraph_diff.py`**, in the same folder, checks constants only. Every
  constant must fit its width. Every constant must also appear in Verilog
  extracted independently from the same graph. The width check runs in the
  CVA6 sweep (`scripts/run_cva6_vc_sweep.sh`) and in the D4 run verifier
  (`scripts/verify_d4_run.sh`). Both checks were run over 83 CORE-ET
  certificates in one recorded sweep (`tests/d4/coreet_122_fresh_gates.tsv`).
  The CORE-ET runner in this directory does not call it.
* **`scripts/mem_lean_diffsim.py`**, at the top of the repository rather than
  in this directory, runs the source RTL against the certificate. It was
  written for mixed-timing memories.

Two things are often mistaken for a check on step 3 and are not:

* `scripts/v2v_verilator_diff.py` compares the original RTL against Verilog
  regenerated from the graph. It never reads a certificate, so it cannot see a
  mistake made by the exporter.
* The static gates check the emitted file: its shape in `verified_compiler`
  mode (`vc_gates.py`), and in legacy mode its internal consistency. They read
  only what the exporter wrote, so they cannot tell whether it matches the
  graph.

So step 3 is checked on chosen shapes and on constants. Nothing checks its full
behaviour across the corpus, and nothing proves it.

`evidence/D1_TRANSCRIPTION_BOUNDARY.md` surveys how other projects handle this
same gap, and sets out options for closing it, with a recommendation. The
cheapest real step is to run the four RTL differentials' comparison over the
whole corpus instead of chosen shapes. Nothing from it is implemented yet.

## What is deliberately not in this directory

This directory holds no compiled code. The implementation is shared with other
LiveHD directions, so moving it here would break them.

| what | where it lives | why it stays there |
|---|---|---|
| the exporter, LGraph to `DesignCert` | `pass/lean/Exporter/Frontend/`, `pass/lean/Exporter/DesignCert/` | every route uses the front half, and three routes use the `DesignCert` writer |
| the simulator and its proofs | `formal/lean/LeanSemanticPrimitives/Compiler/` | it is one Lake package with a pinned toolchain and a Mathlib dependency; 17 of its 33 modules are imported from elsewhere, `CompileDesign` 13 times |
| the proof tools | `pass/lean/tools/` | the HDLProof engine, under active development upstream |
| other routes' tests | `pass/lean/Legacy_Fast_Model_Simulator/tests/`, `pass/lean/Translation_Validation/`, `pass/lean/specialization/` | each route keeps its own |
| scripts several routes share | `pass/lean/scripts/` | the census and parity gates, the axiom audit, and the 12x5 coverage harness |

`pass/lean/README.md` documents the exporter and links here.
