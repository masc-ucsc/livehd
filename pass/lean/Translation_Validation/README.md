# Translation_Validation — the translation-validation simulator

For each hardware design, this simulator turns the design's certificate into
one straight-line Lean function: a compiled simulator with no interpreter loop
and no dispatch on node kinds. It then checks that function against the
semantics that LiveHD's Lean compiler is proved correct for.

"Translation validation" means the tool is not trusted to be correct for every
design. Instead, each design's output is checked on its own. The check is a
sampled differential test by default. With `--prove` it becomes a
kernel-checked theorem.

```text
pass/lean/Translation_Validation/
  scripts/   the sweep driver (d3_sweep.py), merge/join of result tables,
             the corpus driver (run_d3_corpus.sh), generators, unit tests
  tests/     controls that run the driver end to end, and the fixture
             certificate (tests/fixtures/tiny_sync_ram_Lgraph.lean)
```

The Lean side lives in the shared library,
`formal/lean/LeanSemanticPrimitives/Compiler/`:

| file | what it holds |
|---|---|
| `DesignSemantics.lean` | `interpretDesign`, the reference semantics of a certificate |
| `CompileDesign*.lean` | the compiler `compileDesign` and its correctness proof |
| `ResidualSemantics.lean` | `denoteResidual`, the meaning of the compiler's output |
| `ReifyGen.lean` | the reifiers: `reify_design` and its `_named`, `_shared`, `_chunked` forms |
| `ReifyProof.lean`, `Reify.lean` | the per-design proof generators (`prove_reified*`) |
| `D3Harness.lean` | the stimuli, the differential check, the mutation controls, the gate report |

## How it works

```text
RTL ──lhd──▶ LGraph ──pass.single_edge──▶ one clock edge
    ──pass.lean (verified_compiler)──▶ <top>_Lgraph.lean   (DesignCert D)
    ──reify_design──▶ def d3_fast (e : ClockEdges) (i : RuntimeInput)
                                  (st : RuntimeState) : RuntimeResult
```

1. **The certificate.** `pass.lean` in `verified_compiler` mode writes the
   design as a `DesignCert` literal: sources, nodes, flops, memories, outputs
   and clocks.
2. **Compile.** `reify_design D as F` runs the proved compiler `compileDesign`
   on `D` while Lean elaborates the file. The result is a `ResidualProgram R`,
   a flat list of bindings.
3. **Reify.** The reifier writes `R` out as Lean source: one `let` per binding,
   then the outputs and the next state. This `def` is the simulator. It
   evaluates each node once per step, in order, with no lookup.
4. **Check.** The harness runs the simulator and compares it with
   `denoteResidual R` on 32 stimuli. The stimuli are four edge cases (all
   zeros, all ones, lowest bit, highest bit) followed by pseudo-random values.
   The harness also mutates an output, a flop and a memory, and requires each
   mutation to be detected. If the check cannot catch a planted error, no
   agreement result is credited.
5. **Prove (optional, `--prove`).** A second probe generates and kernel-checks

   ```lean
   theorem d3_fast.correct : ∀ e i st, d3_fast e i st = interpretDesign D e i st
   ```

   and then audits its axioms. Only `propext`, `Classical.choice` and
   `Quot.sound` are allowed. `sorryAx`, and the axioms that `native_decide`
   adds, fail the gate.

Each design gets one result row. The row records which gates it passed, in
this order:

| gate | passes when |
|---|---|
| `cert` | the certificate file exists and defines `<top>_designCert` |
| `compile` | `compileDesign` accepted it (observed while elaborating) |
| `reify` | the whole compiled program was written out as a `def` |
| `typecheck` | that `def` elaborated without error |
| `sim` | the `def` ran on one stimulus |
| `checker` | the mutation controls were rejected, so the check can fail |
| `agree` | the `def` equals `denoteResidual` on every sampled stimulus |
| `proof` | `d3_fast.correct` was kernel-checked with allowed axioms (`na` unless `--prove`) |

A row's verdict is the last gate it passed. A resource stop (`timeout`,
`rss_killed`, `deferred`) means no gate was judged. It is not a design
failure, and `--resume` runs that row again.

There are four reifiers (`--reifier`). The simulator and proof stages always
use the same one.

| reifier | shape of the emitted simulator | proof |
|---|---|---|
| `legacy` (default) | one let-chain | `prove_reified`; it cannot close the flop update of a stateful design |
| `named` | one named model shared by the simulator and the proof | `prove_reified_incr`, a step-by-step walk |
| `shared` | each binding computed once | no proof bridge; `--prove` is refused |
| `chunked` | bindings grouped into chunks (`--chunk-size`) | `prove_reified_chunked`, or `--proof-backend module` |

## Build

1. **`lhd`**, to write certificates:

   ```sh
   bazel build -c opt //lhd:lhd
   ```

   If the host compiler is GCC 16, pin an older one, e.g.
   `--repo_env=CC=/usr/bin/gcc-15`. Do not change warning options instead.

2. **Lean.** The toolchain is pinned in `formal/lean/lean-toolchain`
   (Lean 4.31.0) and installed by `elan`. Fetch the pinned Mathlib build:

   ```sh
   cd formal/lean && lake exe cache get
   ```

3. **The fourteen compiler modules, built by explicit name.** A default
   `lake build` does not cover them, so its success says nothing about them.

   ```sh
   pass/lean/scripts/private_compiler_build.sh <checkout> <out-dir>
   ```

   Run it from outside the checkout, and keep `<out-dir>` outside the checkout
   too. It prints `VALIDATED` when all 14 artifacts were rebuilt and an
   explicit `--no-build` pass agrees. A manifest run refuses to start when
   one of them is missing.

## Run

Run the commands from the repository root. A run against a manifest refuses a
dirty tree; do not override that with `--allow-dirty`.

### Simple example: the tracked fixture, with the proof

`tiny_sync_ram` has one flop and one 4-entry by 8-bit memory: 8 nodes in all.

```sh
python3 pass/lean/Translation_Validation/scripts/d3_sweep.py \
  --certs pass/lean/Translation_Validation/tests/fixtures \
  --out out/tiny.tsv --jobs 1 --timeout 900 --samples 32 \
  --reifier named --prove
```

```text
[1/1] tiny_sync_ram    agree    9.27s rss=6860800kB
  ...  agree 1/1   proof 1/1  (0 na, 0 failed)
```

The row reads `proof=1`, `proof_axioms=[propext, Classical.choice,
Quot.sound]`, `distinct_obs=32`, and `mut_out`, `mut_flop`, `mut_mem` all
`rejected`.

### Complicated example: a CVA6 block from RTL

Write the certificate, then sweep it under a hard memory guard. The CVA6
rows are compiled from a wrapper file plus CVA6's own file list:

```sh
lhd compile verilog <top>.sv --reader yosys-slang --top <top> \
    --set yosys.filelist_file=<cva6-filelist>.f --set yosys.setundef=zero \
    --emit-dir lg:out/s1 -- --ignore-assertions                        # RTL -> LGraph
lhd pass single_edge --top <top> lg:out/s1 --emit-dir lg:out/s2        # one clock edge
lhd compile lg:out/s2 --top <top> --emit-dir lean:out/lean \
    --set formal.lean.mode=verified_compiler                           # -> <top>_Lgraph.lean

python3 pass/lean/Translation_Validation/scripts/d3_sweep.py \
  --certs out/lean --out out/d3.tsv --jobs 1 --samples 32 --timeout 7200 \
  --enforce cgroup --kill-over-rss-kb 20000000
```

A single-file design needs only `lhd compile top.sv --reader slang --top
<top> --emit-dir lg:out/s1` as the first step. CORE-ET blocks add the slang
flag `--relax-enum-conversions`. `--enforce cgroup` puts each probe in its own
cgroup with a hard memory limit, and records the cgroup's peak. Use
`--jobs 1` on a shared machine.

`cva6_mmu_gate`, the CVA6 MMU with its TLBs and page-table walker, run this
way (legacy reifier, no proof):

| nodes | sources | flops | total bit width | result | wall | peak memory |
|---:|---:|---:|---:|---|---:|---:|
| 12,263 | 7,537 | 740 | 654,535 | `agree`, 32 distinct observations, both mutations rejected | 2,084 s | 15.2 GB |

### Corpora, tables and tests

* `scripts/run_d3_corpus.sh coreet|cva6|coreet-master|cva6-master` sweeps a
  manifest's certificates, then joins the results onto the manifest's full
  denominator. A target without a certificate keeps its row, marked
  uncreditable.
* Each sweep writes `<out>.tsv` and `<out>.tsv.meta.json`. The meta file holds
  the commit, the tool and artifact digests, the settings and the selection.
  `d3_merge.py` combines tables only when those settings match.
  `d3_join.py --meta` exits 0 only when every certificate the manifest names
  has a judged result with a matching hash.
* The generated probes stay in `temp/d3_sweep/runs/<run_id>/probes/`.
  `<m>.lean` is the simulator and its check; `<m>.proof.lean` is the proof.
* Unit suites: `scripts/d3_{manifest,merge,sweep_parser,sweep_cancel,cgroup,scheduler,runner,proof}_test.py`.
  End-to-end controls: `tests/*_controls.*` and
  `scripts/d3_{resume,module_cli,module_admit}_controls.sh`.

## Trust boundary

| link | how it is established |
|---|---|
| RTL → LGraph → `DesignCert` | **Trusted.** The front end, `pass.single_edge` and the C++ exporter are not proved. They are checked by differential tests elsewhere (`../Lgraph_IR_Semantics_Simulator/`). |
| `DesignCert` → `ResidualProgram` | **Proved once, for every design** (`compileDesign_correct`). Inside `reify_design` the compiler is run by Lean's evaluator; the proof path re-derives the compiled program in the kernel with `rfl`. |
| `ResidualProgram` → simulator (default) | **Tested per design:** 32 sampled stimuli, with mutation controls. This is not a proof; coverage is a lower bound. |
| `ResidualProgram` → simulator (`--prove`) | **Proved per design:** `d3_fast.correct`, kernel-checked, with only the three standard axioms. |
| running the simulator | **Trusted:** Lean's evaluator runs the `def`. |
| Lean 4.31.0 kernel, Mathlib | **Trusted.** |

The certificate's own `<top>_step_correct` theorems use `native_decide`. The
simulator's proof does not depend on them.

**Where the proof stands today.** On current certificates the per-design proof
completes on the fixture. It has not yet completed on a real block. On
CORE-ET's `txfma_f4` (1,867 nodes), the in-process proof failed after 953 s at
88 GB, and the module backend was stopped by its memory guard. So for every
benchmark below, the simulator is checked by the sampled differential test, and
`proof` is `na`.

## Benchmarks covered

### The 12 benchmark rows (CVA6, CORE-ET, PicoRV32)

Measured 2026-10-26: one row at a time, legacy reifier, 32 samples, a
7,200 s time limit and a 19.5 GB memory limit.

| row | design | block | nodes | result | wall | peak memory |
|---|---|---|---:|---|---:|---:|
| B11 | `minion_tlb` | CORE-ET TLB | 467 | `agree` (has memory) | 6 s | 1.7 GB |
| B12 | `picorv32` | PicoRV32 | 2,365 | `agree` (has memory) | 51 s | 2.5 GB |
| B10 | `frontend_gate` | CVA6 frontend | 6,421 | `agree` | 1,202 s | 8.1 GB |
| B09 | `issue_stage_gate` | CVA6 issue / scoreboard | 7,017 | `agree` | 4,338 s | 8.0 GB |
| B05 | `shirecache_mesh_master` | CORE-ET ET-Link → AXI | 10,336 | `agree` (has memory) | 4,492 s | 11.7 GB |
| B04 | `cva6_mmu_gate` | CVA6 MMU / TLB / PTW | 12,263 | `agree` | 2,084 s | 15.2 GB |
| B06 | `load_store_unit_gate` | CVA6 LSU | 15,909 | stopped at the memory limit | 2,107 s | ≥ 19.5 GB |
| B02 | `cva6_hpdcache_subsystem_gate` | CVA6 HPDcache | 25,005 | stopped at the memory limit | 3,553 s | ≥ 19.5 GB |
| B01 | `minion_dcache_top` | CORE-ET D-cache | 25,235 | stopped at the memory limit | 3,152 s | ≥ 19.5 GB |
| B03 | `icache_top` | CORE-ET I-cache | 25,690 | stopped at the memory limit | 3,328 s | ≥ 19.5 GB |
| B08 | `intpipe_top` | CORE-ET integer pipe | 27,121 | stopped at the memory limit | 6,990 s | ≥ 19.5 GB |
| B07 | `vpu_top` | CORE-ET VPU | — | no certificate: `pass.single_edge` refuses a clock gate whose clock is itself gated | — | — |

No row disagrees. The rows marked "has memory" also passed the memory-mutation
control. A row stopped at the memory limit has no verdict yet.

### CORE-ET, 122 blocks

On the CORE-ET blocks whose certificates master emits (measured 2026-10-10;
legacy reifier, 32 samples):

* **76 of 122 `agree`**, and none disagree.
* The other 46 have no certificate, because the emission flow did not produce
  one, so they cannot be credited.
* Wall time per block: median 5 s, largest 242 s (`vpu_mask`, 14,860 nodes).

The older imported CORE-ET 122 and CVA6 30 corpora were emitted before clocks
were added to the certificate format. They no longer typecheck, and results
measured on them do not carry over.
