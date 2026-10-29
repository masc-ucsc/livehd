# pass/lean/specialization: the Futamura-projection (FP) simulator

The FP simulator turns one LiveHD design into a cycle simulator. Every step it takes
is proved equal to the reference semantics of the design. It is built by the first
Futamura projection: a Lean interpreter of hardware designs is specialized to one
design, and what is left is a program for that design alone.

This folder holds the simulator's scripts, tests and evidence. Its Lean code
stays in the Lean project ([Code layout](#code-layout)).

**Terms used below:**
- **Certificate** (`<top>.dcert`, format DCERT2): the design as LiveHD exports it.
  It lists the sources, nodes, flops, memories and clocks. In Lean it is a `DesignCert`.
- **Reference semantics** (`Compiler.interpretDesign`, `Compiler/DesignSemantics.lean`):
  the meaning of one clock cycle of a certificate.
- **Interpreter** (`hwMemAPF`): a program in a small functional language
  (`ObjectLanguage.lean`, semantics `Eval` in `ObjectLanguageSemantics.lean`). It takes
  the design, the clock edges, the inputs and the state, and computes one cycle.
- **Specializer** (`mixDriver`, a partial evaluator): runs the interpreter with the
  design known and the other three arguments unknown.
- **Residual**: the program the specializer returns. It takes edges, inputs and state,
  and runs one cycle of this one design.

## How it works

```
RTL --lhd compile--> LGraph --single_edge--> LGraph --verified_compiler emit--> top.dcert
                                                                                   |
top.dcert --decode + admission--> D : DesignCert                                   |
                                                                                   v
generate:  mixDriver sf wf hwMemAPF [encDesign D]  ==>  residual R : Program
check:     Hw.checkResidual R                      ==>  accepted, step bound
execute:   for each cycle k:  (edges_k, inputs_k, state_k) --R--> (outputs_k, state_k+1)
           and, unless nocompare:  compare with interpretDesign D edges_k inputs_k state_k
```

1. **Export.** LiveHD reads the RTL (yosys-slang), lowers every flop to one clock edge
   (`single_edge`), and writes the certificate (`formal.lean.mode=verified_compiler`).
2. **Decode and admit.** The driver reads the certificate (`readCertChecked`). It
   refuses a certificate that is not well formed, has edge vectors of the wrong size,
   or names one raw input as a reset with two polarities (`RunAdmission`). The memory
   route then checks that it can type every node and memory (`admitsB`).
3. **Generate.** The specializer runs the interpreter on the design. Everything that
   depends only on the design is computed now: node order, widths, opcodes and slot
   lookups. The residual keeps only the work that depends on edges, inputs and state.
4. **Check.** `Hw.checkResidual` walks the residual. It accepts only programs in the
   fragment the runtime executes, and returns the fuel bound one step needs.
5. **Execute.** `runMemSim` runs the residual one cycle at a time. Before each cycle,
   the runtime policy `runtimeFullOKB` checks that the edges, inputs and state fit the
   design: widths, memory sizes, clock count. By default each cycle is also run through
   `interpretDesign`, and the run fails on any difference.

### Two routes

| route | interpreter | designs | drivers |
|---|---|---|---|
| memory (default) | `hwMemAPF` (`MemFused.lean`) | all, with or without memories | `dcert2_memroute` |
| scalar | `hwAPVarT` | memory-free designs only | `gen_probe_v2`, `gen_readiness_v2` |

The memory route's interpreter was rewritten twice for speed. Both rewrites are proved
to compute the same results as the original (`memVar_iff`, `memF_iff`):
- `MemVariant`: the list of sources is no longer bound by a `let`. A dynamic `let`
  makes the specializer keep the list in the residual, and every later read became a
  chain of `tl`s.
- `MemFused` (the default, `route:fused`): the fold over the nodes is fused into its
  readers.

Residual size and specialization time (native -O2, from `M3_UNBOUNDED_PROOFS.md`):

| row | original interpreter | `MemVariant` | `MemFused` |
|---|---|---|---|
| B05 CORE-ET ET-Link→AXI | unfinished after 5 h | 6.1M terms (14 s) | 192k terms (5 s) |
| B01 CORE-ET DCache | unfinished after 8 h | 37M terms (108 s) | 3.3M terms (30 s) |
| B08 CORE-ET IntPipe | unfinished after 5 h | 175M terms (634 s) | 17.7M terms (69 s) |

### What is proved

| theorem | file | says |
|---|---|---|
| `mixDriver_correct` | `PartialEvaluatorCorrect.lean` | the residual, run on the unknown arguments, gives exactly the values the interpreter gives on all arguments |
| `mixDriver_eq_S3` (`@[csimp]`) | `S3Correct.lean` | the fast specializer S3 equals the proved one, fuel and errors included; compiled code runs S3 |
| `memF_iff` | `MemFused.lean` | the fused interpreter evaluates exactly as the original memory interpreter on every admitted step |
| `projectMemFDesignAt_correct` | `MemFused.lean` | if specialization returned `R`, the design is admitted and the policy accepts the edges, inputs and state, then `R` returns exactly one result; that result means `interpretDesign D e i st`, and its next state passes the policy again |
| `runMemSimF_correct` | `MemFused.lean` | the same for one step of a checked simulator bundle (`mkMemSimOf`, `mkMemSimOf_wf`) |

The next state passes the policy again. So when the next cycle's edges and inputs pass
it too (the runtime checks this), the theorem applies again, and a run of any length
agrees with `interpretDesign` at every cycle.

## Build

Lean 4.36.0-rc1 is pinned in `formal/lean/lean-toolchain`. Run Lean commands from
`formal/lean`.

```bash
# 1. lhd, the exporter
bazel build -c opt //lhd:lhd

# 2. check the Lean library, the specializer and its proofs, and the axiom gate
#    (Mathlib-free, plain `lean`, into formal/lean/.olean-dev)
cd formal/lean
bash .build-proj.sh

# 3. the native drivers (-O2), into a project-local root
ROOT=$PWD/../../generated/fp/nat_root
OPT=-O2 ROOT=$ROOT bash Futamura_Projection_Scripts/build-opt.sh Futamura_Projection_Scripts/dcert2_memroute.lean \
    Futamura_Projection_Scripts/dcert2_loadcheck.lean Futamura_Projection_Scripts/dcert2_support.lean \
    Futamura_Projection_Scripts/gen_probe_v2.lean Futamura_Projection_Scripts/gen_readiness_v2.lean
# -> $ROOT/nat/dcert2_memroute, ...

# 4. self-tests: the comparison and state-file rules, then the four small fixtures
$ROOT/nat/dcert2_memroute --selftest                        # ends with SELFTEST PASS
$ROOT/nat/dcert2_memroute --fixture twomem 200000 5000 4    # also: full, mixed, lane
```

`.build-proj.sh` fails if an audited theorem rests on any axiom other than `propext`,
`Classical.choice` and `Quot.sound` (the gate is `#audit_axioms`, in
`Projection/AxiomGate.lean`). The theorems are audited in `Projection/Audit.lean` and
at the end of the module that proves them (`MemFused.lean`, `MemProjection.lean`).

## Run

**1. Export the design** (`$W` is a work folder):

```bash
lhd compile verilog top.sv --reader yosys-slang --top top --set yosys.setundef=zero \
    --emit-dir lg:$W/s1 --workdir $W/w1          # a file list: --set yosys.filelist_file=<f>
lhd pass single_edge --top top lg:$W/s1 --emit-dir lg:$W/s2 --workdir $W/w2
lhd compile lg:$W/s2 --top top --emit-dir lean:$W/lean --workdir $W/w3 \
    --set formal.lean.mode=verified_compiler
# -> $W/lean/top.dcert
```

**2a. Run the simulator directly:**

```bash
dcert2_memroute $W/lean/top.dcert 20000000 500000 4 zero
#               certificate       stepFuel workFuel cycles init
```

- `stepFuel`, `workFuel`: the specializer's fuel. Running out is reported as exit 3,
  never as a refusal of the design.
- `cycles`: must be positive.
- `init`: the initial state. `zero` (empty memories, flops at their reset values) is a
  synthetic legal state, not the RTL's startup state. `file:<path>` reads a supplied
  state. `ramp` and `narrow` are other synthetic controls.
- More options: `stim:<file>` (supplied inputs), `nocompare` (no reference run),
  `trace` (per-cycle memory traces), `route:fused|var|plain`.
- Exit codes: 0 ok, 1 execution failure, 2 configuration refused, 3 out of fuel,
  4 specialization failed, 5 residual check rejected, 6 memory route refuses the
  design, 7 runtime policy refuses a state, input or edge vector, 8 parse failure.

The last line of a passing run reads:

```
RESULT: MEMROUTE OK (admitted, specialized, checked, executed at the returned bound,
returned state passes the policy, and the reference comparison is COMPLETE)
```

**2b. Or through the recorded runner**, which keeps every run as an immutable attempt
folder, with its identity (certificate, driver and argument hashes), timing, CPU,
peak memory and exit status:

```bash
bash pass/lean/specialization/scripts/fp_cov_dcert2.sh --native $ROOT/nat --only memroute \
    --memmax 32G <livehd-checkout> <out-dir> 20000000 500000 4 $W/lean/top.dcert
```

- Phases: the default runs the scalar route's four, `decode`, `support`,
  `generate` and `readiness`. The memory route, which the benchmark rows use,
  runs only with `--only memroute`, as above.
- `--memmax` is a memory ceiling for host safety. A run it stops is recorded as
  KILLED, never as a refusal by the design. There is no time limit.
- `<livehd-checkout>` supplies `LEAN_PATH` for the interpreted route (no `--native`),
  and its HEAD is recorded.

## Trust boundary

**Trusted (not proved):**
- The RTL front end and LiveHD's passes up to the certificate: yosys-slang,
  `single_edge`, and the C++ exporter. The certificate is taken as the meaning of the
  RTL. The Lean certificate reader and writer are proved inverse
  (`Compiler/CertIORoundTrip.lean`), but nothing proves the C++ exporter's
  translation of the LGraph.
- The native run of the specializer. The premise `projectMemFDesignAt sf wf D = .ok R`
  is established by running compiled code, not by the kernel. The comparison against
  `interpretDesign` on every run checks it.
- Lean's compiler and runtime, the C compiler that builds the drivers, and the Lean
  kernel.

**Proved once, for all designs:** the specializer, the S3 speed-up, the fused
interpreter, and the step theorem (the table above). Each of them passes the axiom
gate.

**Checked on every run:** admission, `Hw.checkResidual`, the runtime policy on every
cycle, and (unless `nocompare`) equality of outputs and next state with
`interpretDesign`.

**What a run claims:** for the cycles run, with the stated initial state and inputs,
the simulator's outputs and next states equal those of `interpretDesign`. The
synthetic `zero` state is a legal state, not the RTL's reset sequence.

## Benchmarks

The 12 rows of LiveHD's Lean coverage table, from the baseline RTL of each block.
Measured 2026-10-20 with the `lhd` of master at `2353ee77a`: memory route, `MemFused`,
fuel 20000000/500000, `init=zero`, 4 cycles each compared with `interpretDesign`.

| row | block | top | nodes | flops | memories | residual terms | step bound | 4 compared cycles |
|---|---|---|---|---|---|---|---|---|
| B11 | CORE-ET TLB | `minion_tlb` | 467 | 30 | 1 | 8,917 | 1,078 | OK, 1.7 s |
| B12 | PicoRV32 | `picorv32` | 2,365 | 174 | 1 | 121,807 | 5,760 | OK, 71 s |
| B10 | CVA6 frontend | `frontend_gate` | 6,421 | 409 | 0 | 614,422 | 19,330 | OK, 5,349 s |
| B09 | CVA6 issue stage, scoreboard | `issue_stage_gate` | 7,017 | 334 | 0 | 451,147 | 19,344 | OK, 23,976 s |
| B05 | CORE-ET ET-Link→AXI (2 clocks) | `shirecache_mesh_master` | 10,336 | 83 | 13 | 192,815 | 38,949 | OK, 7,977 s |
| B04 | CVA6 MMU, TLB, PTW | `cva6_mmu_gate` | 12,736 | 740 | 0 | 1,876,666 | 30,100 | OK, 13,139 s |
| B06 | CVA6 load/store unit | `load_store_unit_gate` | 16,370 | 876 | 0 | 2,605,578 | 39,029 | OK, 22,350 s |
| B02 | CVA6 HPDcache | `cva6_hpdcache_subsystem_gate` | 24,995 | 542 | 12 | 1,231,570 | 60,558 | stopped after 9.1 h |
| B01 | CORE-ET DCache | `minion_dcache_top` | 25,235 | 990 | 21 | 3,291,988 | 60,759 | stopped after 10.6 h |
| B03 | CORE-ET I-cache | `icache_top` | 25,690 | 505 | 11 | 1,097,557 | 65,583 | stopped after 10.6 h |
| B08 | CORE-ET integer pipeline | `intpipe_top` | 27,121 | 2,402 | 1 | 17,682,706 | 71,538 | stopped after 10.6 h |
| B07 | CORE-ET VPU | `vpu_top` | — | — | — | — | — | no certificate: `single_edge` stops on a clock-gate latch (`clock-gate-latch-phase`) |

- 11 of 12 rows generate a simulator that `Hw.checkResidual` accepts.
- 7 rows finished 4 compared cycles. On B01, B02, B03 and B08 the runs were stopped
  by hand during execution. Their generation and check had finished.
- The CVA6 rows wrap the upstream module in a `*_gate.sv` top and read the
  `cv64a6_imafdc_sv39_hpdcache_wb` file list.

**Earlier CVA6 blocks, scalar route:**
- Combinational: `alu`, `alu_wrapper`, `commit_stage`, `compressed_decoder`, `decoder`,
  `instr_scan`, `pmp`, `pmp_data_if`. Their 26 historical passes were re-run at a later
  HEAD and all passed again (`evidence/fp_b6_head_replay.tsv`).
- Sequential: `ariane_regfile`, `serdiv`, `instr_queue`, `btb`, `store_buffer`,
  `id_stage`, `cva6_ptw`, `perf_counters`, `bht` (`evidence/fp_frozen30_seq_join.tsv`).

## Performance

Two examples from the 2026-10-20 runs above. All drivers are native -O2.

### Simple: CORE-ET TLB (B11)

The certificate has 316 sources, 467 nodes, 5 outputs, 30 flops, one memory of 8 words
× 69 bits, and one clock.

```bash
dcert2_memroute minion_tlb.dcert 20000000 500000 4 zero
```

| phase | time | result |
|---|---|---|
| admission | 10 ms | scalar route refused (memory); memory route admitted |
| specialize | 76 ms | residual of 8,917 terms: one function, 3 arguments |
| check | 1 ms | step bound 1,078 |
| execute and compare, 4 cycles | 1.6 s | `ok,ok,ok,ok`; comparison COMPLETE |
| whole process | 1.74 s wall | peak memory 64 MiB |

### Complicated: CVA6 load/store unit (B06)

The certificate has 10,709 sources, 16,370 nodes, 21 outputs, 876 flops and one clock.

```bash
dcert2_memroute load_store_unit_gate.dcert 20000000 500000 4 zero
```

| phase | time | result |
|---|---|---|
| admission | 48 ms | memory route admitted |
| specialize | 3.9 s | residual of 2,605,578 terms |
| check | 0.11 s | step bound 39,029 |
| execute and compare, 4 cycles | 22,346 s (6.2 h) | `ok,ok,ok,ok`; comparison COMPLETE |
| whole process | 22,350 s wall | peak memory 185 MiB |

### Reading the numbers

- **Generation is fast.** The LSU's 2.6M-term residual is built in 3.9 s and checked
  in 0.11 s.
- **Compared execution is slow on large blocks.** The execute phase runs the residual
  and `interpretDesign` on every cycle. Both compute with the same bit-level model
  (`Translation/LGraphModel.lean`): a bitwise operation on a `w`-bit value tests each
  bit and adds powers of two (`bits_to_int`). The split between the residual and the
  reference was not measured on these rows.
- **What specialization buys** was measured on the scalar route
  (`evidence/fp_speedup.tsv`): the same six stimuli run through `interpretDesign` and
  through the residual, native -O2, in one process. The residual is 17–97× faster on
  8 sequential CVA6 blocks (mean 49×), for example `instr_queue` 97×, `btb` 90× and
  `id_stage` 27×. It is 0.9–22× faster on 8 combinational blocks (mean 5.9×).
  Specialization is paid once and is not inside these ratios.

## Unbounded proofs

The same residual is the input of the unbounded proof tools (invariants, k-induction,
refinement with stuttering): see `M3_UNBOUNDED_PROOFS.md` and
`pass/lean/tools/README.md`.

## Code layout

One rule decides where a file goes:
- `scripts/`: what you run to produce a result;
- `tests/`: what you run to check the tool, with fixtures in `tests/fixtures/`;
- `evidence/`: measurements and run records, kept as records. Nothing reads them.

| `scripts/` | role |
|---|---|
| `fp_cov_dcert2.sh` | the recorded runner: decode, support, generate, readiness and memroute phases; immutable attempt folders, reuse only on identical identity, memory ceiling, no time limit |
| `fp_gen_build.sh`, `fp_gen_sample.sh`, `fp_gen_measure.sh`, `fp_gen_counterbalanced.sh` | generation-latency builds and counterbalanced measurements |
| `fp_install.py` | historical: installed the projection branch's files onto master, at the paths they had then (`formal/lean/scripts/...`); its map is `evidence/fp_install_map.tsv` |
| `fp_replay.py` | replays historical FP rows into a fresh output folder |
| `coreet_operator_census.py` | which operators the CORE-ET certificates use, and whether the interpreter implements each (`coreet_operator_census.md`) |

| `tests/` | run |
|---|---|
| `fp_cov_dcert2_reuse_test.sh`: the runner's identity, reuse and attempt rules | `bash pass/lean/specialization/tests/fp_cov_dcert2_reuse_test.sh` |
| `fp_replay_controls.py`: `fp_replay.py`'s admission rules | `python pass/lean/specialization/tests/fp_replay_controls.py` |
| `axiom_gate_controls.sh`: the axiom gate refuses `sorry`, `native_decide` and an explicit axiom | `bash pass/lean/specialization/tests/axiom_gate_controls.sh` |
| `check_overlay.sh`: the shared semantics have not drifted from their base | `bash pass/lean/specialization/tests/check_overlay.sh [--semantic]` |
| `check_certio.sh`: the provenance of the DCERT1 codec (`Compiler/CertIOV1.lean`) | `bash pass/lean/specialization/tests/check_certio.sh` |
| `fp_sext_controls.lean`, `fp_dcert1_admission_controls.lean` | from `formal/lean`: `LEAN_PATH=.olean-dev lean ../../pass/lean/specialization/tests/<file>` |
| `fp_csimp_checks.lean`: the `@[csimp]` equalities and their axioms | it needs the `Proto/` modules, which only `build-opt.sh` builds: `LEAN_PATH=$ROOT/olean lean ...` |

`evidence/` holds the coverage, admission, generation and readiness tables
(`fp_*.tsv`, `fp_master_experiments.jsonl`), the speed-up table `fp_speedup.tsv`,
the generation-only procedure (`fp_generation_only.md`), the CertIO experiment
log and profiles (`certio/`), and the FP branch's historical overlay
(`bv_shl_step.patch`).

The Lean code stays in the Lean project:
- `formal/lean/LeanSemanticPrimitives/Projection/`: the interpreter, the
  specializer, the proofs, the memory route and the axiom gate;
- `formal/lean/Futamura_Projection_Scripts/`: every file is this direction's.
  It holds the drivers (`dcert2_*`, `gen_probe_v2`, `gen_readiness_v2`,
  `cert_runner_any`, `cert_trace`, ...), the build scripts `build-opt.sh` and
  `build-native.sh`, the helpers next to them, and the state and stimulus files
  in `memstate/`. It was `formal/lean/scripts/`; recorded runs keep that path.
  Two other users: the proof tools build with its `build-opt.sh`, and the
  exporter's RTL oracles (`pass/lean/Exporter/Frontend/scripts/*_oracle.sh`) run
  its `cert_trace.lean`.

Elsewhere:
- the proof tools that use the residual: `pass/lean/tools/` (their design note,
  `M3_UNBOUNDED_PROOFS.md`, is in this folder);
- the exporter: `pass/lean/Exporter/`;
- the 12-row coverage harness that every route uses: `pass/lean/scripts/fp_cov_run.sh`
  and the `fp_cov_queue_*.sh` queues.
