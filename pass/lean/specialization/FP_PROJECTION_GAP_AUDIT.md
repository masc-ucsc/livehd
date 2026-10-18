# FP gap audit: master's Futamura projection vs the `projection` branch

Audit before rebase. Every difference between master's Futamura-projection
(FP) implementation and the `projection` branch gets a side, a topic and a
disposition, so that nothing is ported, dropped or re-derived by symptom.
Analysis only: written 2026-10-16, read-only, nothing built or run.

## Revisions compared

| | checkout | ref | revision | date |
|---|---|---|---|---|
| master | `livehd-pass-lean-master` | `master` | `b9b404981` | 2026-10-14 |
| FP | `livehd-futamura/livehd` | `projection` | `9bb41a09d` (clean tree) | 2026-10-06 |
| merge base | | | `bc9fcb1ba` | 2026-08-27 |

`PORT_LEDGER_D3_D4_FP.md` §0 pins FP at exactly `9bb41a09d`, so every
projection commit is in scope. `9bb41a09d` is present in this repository.

## File-level summary

- The projection branch added or modified **94 files** since the merge base
  (`git diff --name-only bc9fcb1ba 9bb41a09d`): 75 under
  `formal/lean/LeanSemanticPrimitives`, 17 under `formal/lean/scripts`,
  `formal/lean/.build-proj.sh` and `.gitignore`. No C++.
- On master: **61 byte-identical, 33 modified, 0 deleted.**
- master adds the projection-only modules `HardwareMem*`, `Mem*` and
  `Proto/ShiftBridge`: 50 top-level / 64 recursive `Projection` modules, against
  25 / 38 at the pin (ledger §63.84).
- 67 master commits touch projection code after the pin
  (`git log 9bb41a09d..HEAD -- formal/lean/LeanSemanticPrimitives/Projection formal/lean/scripts formal/lean/.build-proj.sh`).

**Byte-identical to the pin.** The scalar FP core is unchanged:
- `PartialEvaluator`, `HardwareInterpreter`, `HardwareAdequacy`, `ProjectedStep`,
  `ProjectionCorrect`, `ResidualFragment`;
- all 13 `Proto/` modules;
- `Audit`, with its 311 axiom obligations.

The shared semantics are also unchanged: `Compiler/{DesignCert,DesignSemantics,Runtime,DesignCertWF}`,
`Translation/GraphRefine` and `SemanticPrimitives`.

**Of the 33 modified files:**
- **Path-redaction only:** `census/*`, `certio/*` including
  `experiments.jsonl`, `certio/perf-artifacts/*` and `overlays/check_overlay.sh`
  (a84a26f09).
- **V1 retargets:** `.build-proj.sh`, `build-opt.sh`, `build-native.sh`, and the
  `proto`/`total`/`s2`/`s2a`/`s3` probes.
- **Real content:** `CertLoad`, `Encoding`, `MixProgram`, `ObjectLanguage`,
  `ObjectLanguageSemantics`, `OperatorBridge`, `PartialEvaluatorCorrect`,
  `Compiler/CertIO`, `Translation/{LGraphModel,OpBridge}`.
- **Other:** `record_experiment.py` (scratch path) and `.gitignore`.

## Key

**Side.** Each item sits in one section, by which side lacks it:
- **A:** the projection branch lacks it; master has it.
- **B:** master lacks it, or master's port is incomplete.
- **C:** both have it, done differently.
- **D:** open on both sides.

**Topic:** Upstream · Exporter/Format · Semantics · FP-code · Proofs · Coverage ·
Workflow · Perf · Docs.

**Disposition:**
- **KEEP-MASTER:** master is the authority; nothing to port back.
- **FIX-MASTER:** a gap to close on master.
- **DECIDE:** needs a policy decision.
- **RECORD:** document only.
- **OPEN:** unresolved on both sides.

**✓** marks a row re-checked directly against source for this audit. Other rows
cite the ledger or commits.

---

## A. What the projection branch lacks (master has it)

### A1. Upstream LiveHD IR changes since the merge base
*Topic: Upstream · Disposition: KEEP-MASTER*

The changes:
- constant-pool pins (9adce39b5);
- one driver per sink, operand banks (42773693a);
- GetMask/SetMask as `[lo,hi)` endpoints (eb91f09c1);
- Concat from the new frontend, lowered to Or/SHL/Sext;
- hotmux lowering (4f255d2a5, 553093249);
- yosys multi-chunk sign (39d340c05);
- Sext-wrapped async-reset tracing (d5ee4dd9e).

The pinned FP predicates admit the resulting shapes. `Op_SetMask` is the
exception (D6).

Evidence: `pass/lean/certificate_ir.cpp:120-138`, `pass/lean/design_scan.cpp`.

### A2. Normalization passes that change what FP receives
*Topic: Upstream · Disposition: KEEP-MASTER*

- `single_edge`:
  - per-port memory slot gate (a34a9603d);
  - gated memory write clock folded into the enable (cdb2b6e60);
  - slot-mask repair (94f251bf1);
  - planning no longer mutates the graph (075d90171).
- `latch_contract` ICG/latch guards (e517351c0, 11fef262f).
- `split_selfref` Sext arm (b9b404981; B04 compiles).
- Forwarding matrix read at arbitrary width (1c49ab04d).
- `formal.lean.max_width` above 1024 is needed for B03.

Evidence: ledger §63.75–§63.88, §59.3.

### A3. master's own exporter
*Topic: Exporter · Disposition: KEEP-MASTER*

The projection branch wrote no C++; it consumed DCERT1 files from the d4
exporter. master writes certificates itself, as **DCERT2 only**:
- the magic is written at `pass/lean/emit_dcert.cpp:143` ✓, in `verified_compiler`
  mode (`pass_lean.cpp:69-90`);
- memory is lowered per read port, and a sync read becomes a synthetic flop
  (`pass/lean/memory_lowering.cpp`).

`design_scan.cpp` refuses these shapes by name:
- constant or multi-bit clock root;
- unnormalized ICG;
- falling-edge memory port;
- `posclk=0`;
- unsupported operators (Rem, Latch, LUT), found by census.

Memory policies are refused at `pass/lean/graph_access.cpp:415-560`.

### A4. DCERT2 format
*Topic: Format · Disposition: KEEP-MASTER*

DCERT2 adds `FlopDesc.clock`, `FlopDesc.asyncReset`, `MemoryDesc.clock` and a
clocks section. It has meaning checks (`certWfError`, `readCertChecked`), and
`parseCert_renderCert` is proved for every DesignCert
(`Compiler/CertIORoundTrip.lean:1046` ✓).

DCERT1 cannot carry `asyncReset`, which 409/409 frontend flops and 333/334
issue-stage flops use.

Evidence: ledger §11, §21, §63.6.

### A5. Reset polarity at the exporter
*Topic: Exporter · Disposition: KEEP-MASTER (classification: C4)*

One scanned `active_low` feeds both the `flopQAsync` source and the `FlopDesc`
(`certificate_ir.cpp:74,345` ✓).

From `tests/fp_reset_polarity_census.tsv`:

| corpus | certificates | raw-port opposite polarities |
|---|---:|---:|
| imported DCERT1 | 59 | 5,837 |
| master DCERT2 | 132 | 0 |

Evidence: ledger §46.2.

### A6. Signed GetMask adapter and constant-mask width
*Topic: Exporter/Semantics · Disposition: KEEP-MASTER*

- **Signed GetMask → Sext adapter** (897049f16). On Pico it changes sources/nodes
  from 1292/2269 to 1355/2332.
- **Constant-mask width and negative-constant fix** (b2d4c4c2b). The pin has the
  same truncation at its `pass_lean.cpp:1317`.

Evidence: ledger §63.64–§63.70.

### A7. `eval_op` boundary repairs
*Topic: Semantics · Disposition: KEEP-MASTER* ✓

In `Translation/LGraphModel.lean`:
- `Op_SHL` saturates at the result width (`k ≥ w` → 0);
- `bv_sra` saturates at the source width;
- `Op_Sext` takes a strict `n > a.width` branch.

FP inherits all three directly: `.bvSra` and `.bvShl` evaluate through
`bv_sra`/`bv_shl_step` (`ObjectLanguageSemantics.lean:270-281` ✓). No other
operator's semantics changed.

### A8. The checked memory route
*Topic: FP-code/Proofs · Disposition: KEEP-MASTER*

About 28 modules. All are proved with standard axioms only, with no
`native_decide` and no `sorry`.
- **Representation:** finite `MemImg {words, dflt}` images (`MemImage`,
  `MemLog`, the `StateM` codec).
- **Object primitives:** `memRead` (32), `memWriteBE` (33), `memWrite` (34) ✓.
- **Entry:** `hwMemS`/`mainM`, with `mainM_adequate(_resolved)`.
- **Static admission:** `admitsB`.
- **Runtime policy:** `runtimeFullOKB`, word-level state preservation,
  `initStateOf`.
- **Specialization:** BTA, `projectMemDesign_correct`, `runMemSim_correct`. The
  residual has arity 3 and passes the unchanged `checkResidual`.

Evidence: ledger §63.15–§63.49.

### A9. Memory support report
*Topic: FP-code · Disposition: KEEP-MASTER*

`MemSupport.report` sits beside the unchanged `CertLoad.supportReport`, which
still refuses memory. `scalar_refuses_memory` is proved.

Evidence: ledger §63.48.

### A10. Plain `Op_MemWrite` route
*Topic: FP-code · Disposition: KEEP-MASTER*

`nodeTyW`; `admitsWB`, which changes nothing for designs without a plain write;
`runMemSimW_correct`. Five retained certificates run COMPLETE through it. It is
**off by default in production.**

Evidence: ledger §63.61, §63.71–§63.74.

### A11. `Proto/ShiftBridge.lean`
*Topic: Proofs · Disposition: KEEP-MASTER*

Step 1 of 5 of the bridge toward a proved S3 (`PVal_shift_add`,
`PEnv_shiftBy_add`). No axioms.

Evidence: ledger §57.5–§57.6.

### A12. DCERT2 drivers in `formal/lean/scripts/`
*Topic: Workflow · Disposition: KEEP-MASTER*

- Scalar route: `gen_probe_v2.lean`, `gen_readiness_v2.lean`,
  `dcert2_loadcheck.lean`, `dcert2_support.lean`.
- `dcert2_memroute.lean`, the production memory route:
  - exits 0–8;
  - `mkMemSimOf` removes a double specialization (659 s → 2.6 s);
  - `memstate/`, plus `stim`/`react` stimulus entries;
  - `--selftest` with 22 controls.
- Census and control probes: `cert_cone_eval`, `cert_mask_eval`,
  `cert_op_census`, `slot_mask_check`, `plainwrite_admit`, `pico_*`.

Evidence: ledger §63.50–§63.59.

### A13. Harness
*Topic: Workflow · Disposition: KEEP-MASTER*

- **Install and replay:** `pass/lean/specialization/fp_install.py` (56 pinned assets,
  0 mismatches) and `fp_replay.py`, which is stricter than
  `record_experiment.py`.
- **Coverage runners:** `fp_cov_dcert2.sh`, `fp_cov_run.sh`, and the
  `fp_gen_*` pin-vs-master timing scripts.
- **Exit taxonomy:** OK / CAP 124 / REFUSED / MISSING 97 / DISAGREE 96.
- **Run discipline:** immutable attempts and per-run cgroup sampling.
- **Tests:** `pass/lean/specialization/tests/*` (`fp_replay_controls.py`: 39 controls) and
  the B11 acceptance gate `pass/lean/tests/d4/b11_tlb_acceptance.sh`.

Evidence: ledger §38–§45, §58, §63.x.

### A14. Coverage
*Topic: Coverage · Disposition: KEEP-MASTER*

12×5 FP column: **4 YES, 8 BLOCKED.**
- Scalar route on DCERT2: B09 issue/scoreboard, B10 frontend.
- Memory route: B11 TLB, B12 PicoRV32.

On the frozen-30 CVA6 set with the historical DCERT1 inputs: 8 combinational +
8 sequential = **16 of the 22 supported**. 6 sequential blocks are undecided
at the wall cap.

The branch's own count was 8 zero-flop blocks.

Evidence: `tests/MASTER_SIM_GENERATION_12X5.tsv`; ledger §49 and lines
5009-5010 ✓.

### A15. Speedup and latency measurements
*Topic: Perf · Disposition: RECORD*

Residual vs `interpretDesign` on 16 designs, native -O2, one process:

| designs | range | mean |
|---|---|---|
| sequential (8) | 17–97× | 49× |
| combinational (8) | 0.9–22× | 5.9× |

There is also a matched pin-vs-master generation-latency study.

Evidence: ledger §50.1 ✓, §55–§56.

---

## B. What master lacks, or where master's port is incomplete

### B1. No Projection-module DCERT2 path
*Topic: Workflow/Format · Disposition: DECIDE*

These read DCERT1 only, through `V1.parseCert` plus `asyncOK`
(`Projection/CertLoad.lean:23,187,200` ✓):
- `CertLoad.runOne` and `cert_runner`;
- `proto_probe`, `total_probe`, `s2_probe`, `s2a_probe`, `s3_probe`;
- `gen_probe`.

master's C++ emits only DCERT2, so the pinned production runner cannot read any
certificate master produces. DCERT2 reaches FP only through scripts that call
`CertIO.admitAny`.

Evidence: ledger §46.4.

### B2. Native route is DCERT1-only
*Topic: Perf/Workflow · Disposition: DECIDE*

The native -O2 FP route exists only for DCERT1. The DCERT2 route runs
interpreted (`lean --run`), because DCERT2 imports Mathlib and Mathlib ships no
native objects. The interpreted route also needs more fuel: `issue_stage` needs
200000/5000.

Evidence: ledger lines 6654-6662, 6744-6769.

### B3. `MixProgram` mirror is missing `memWrite`
*Topic: FP-code · Disposition: FIX-MASTER* ✓

`primTable` (`Projection/MixProgram.lean:168-172`) has codes 32 and 33 only, not
34. A STATIC `memWrite` application in the object-language specializer (the
second projection) therefore answers `typeError`. The ledger does not record
this.

### B4. Overlay check would report drift
*Topic: Workflow · Disposition: FIX-MASTER* ✓

`Projection/overlays/check_overlay.sh:26` expects LGraphModel sha256
`8a4fc7ea…`, the pin's. master's is `4d002b16…`. `Translation/OpBridge.lean`
also differs from semantic pin 90583be.

This is inferred from the hashes; the script was not run.

### B5. Script defaults redacted to `/WORKROOT`
*Topic: Workflow · Disposition: FIX-MASTER or RECORD*

The path redaction (a84a26f09) set defaults to `/WORKROOT/projects/…` in
`certio/check_certio.sh` (SRC_REPO/BASE_REPO), `census/*` and
`overlays/check_overlay.sh`. These no longer run in place without environment
overrides.

The ledger's "0 drift since install" predates the redaction.

### B6. Historical passes not credited
*Topic: Coverage/Evidence · Disposition: RECORD (re-run only if wanted)* ✓

master credits 10 of the branch's 26 historical passes. The other 16 are
"observed-but-evidence-limited": they were recorded before attempt scoping and
were not re-run. Rows 1 and 5 exited 3; rows 13 and 14 have unknown exit.

Evidence: ledger §44.5.

### B7. Prototype paths not promoted
*Topic: Proofs · Disposition: OPEN (needs ShiftBridge steps 2–5)*

All 13 `Proto/` modules are built and executed but not promoted, including
`ProtoFast` (the fork) and `ProtoShift` (S3). `ProtoShift` has zero theorems
and cannot supply `hproj`.

`ProtoAudit`'s 46 reports are separate from the primary 311. The four
`@[csimp]` compiled obligations are exercised only by the runners.

Evidence: ledger lines 3737-3763, 3883-3923.

### B8. No cross-build S1-vs-S2 study
*Topic: Perf · Disposition: RECORD*

Never executed on master.

Evidence: ledger lines 4726-4761.

### B9. Generation ~6% slower on master
*Topic: Perf · Disposition: OPEN* ✓

Cause unknown; the specializer files are byte-identical.

| block | change on master | paired runs |
|---|---:|---|
| `ariane_regfile` | +6.4% | master slower 7 of 7 |
| `alu` | −22.9% | master slower 1 of 7 |

Evidence: ledger lines 5866-5885.

### B10. No residual serialization
*Topic: Workflow · Disposition: OPEN*

Readiness re-specializes. B12 avoids this only by running everything in one
process.

Evidence: ledger lines 6717-6721.

### B11. No-Mathlib build does not cover the memory modules
*Topic: Workflow · Disposition: RECORD*

`.build-proj.sh` builds the same 33 pinned targets. The memory modules are
lake-only, and `HardwareMemTraversal` imports `Compiler.DirectCheck`, which the
branch never imported.

### B12. Stale or contradictory text on master
*Topic: Docs · Disposition: FIX-MASTER (docs)*

- `Compiler/CertIORoundTrip.lean:10-14` ✓ says there is "NO C++ DCERT2
  serializer". `emit_dcert.cpp` exists (2daa01c94).
- `tests/fp_entrypoint_inventory.tsv` ✓ says "THE PIN IS NOT REACHABLE FROM
  THIS REPOSITORY". It is reachable.
- `LGraphModel` docstrings call `bv_shl_step_unfolds` "FALSE" and `bv_sra`
  equality "open". `OperatorBridge` and `OpBridge` prove both.
- `PASS_LEAN_RESTRUCTURE_PLAN.md` §15.5 lines 1096, 1102, 1109, 1349 and 1351
  predate the end of the ledger.
- The `provisional` string in `MASTER_SIM_GENERATION_12X5.json` is stale.
- The B01 and B04 rows predate 0d9acf16d and b9b404981.
- `fp_reset_polarity_census.tsv` cites `certificate_ir.cpp:72/:271`; the lines
  are now :74/:345.

---

## C. Diverged (both have it, done differently)

### C1. Two codecs named `CertIO`
*Topic: Format · Disposition: KEEP-MASTER*

`Compiler.CertIO` is DCERT1 on the branch and DCERT2 (D4's) on master. FP's
codec lives on master as `Compiler.CertIO.V1` (`CertIOV1.lean`). It is verbatim
apart from the namespace and a new §7, which proves `renderCert_not_injective`
over clock metadata.

`CertIOFacade` routes on the magic. `admitV1` skips `certWfError`.

### C2. OperatorBridge proofs redone
*Topic: Proofs · Disposition: KEEP-MASTER*

The four statements are unchanged. The `rfl` / `split <;> rfl` proofs were
redone against master's `eval_op`. The `bv_shl_step` overlay is redefined with
master's guarded body rather than applied as a patch.

### C3. Three edge-vector admission rules
*Topic: Policy · Disposition: DECIDE*

| rule | condition |
|---|---|
| FP `ProjectedStep.runtimeOK` | `clocks.size ≤ edges.size` |
| `Direct.checkRuntime` | exact match |
| memory route `edgesOKB` | per element: `clock < e.size` |

The ledger compares the first two; it never compares the third with either.

### C4. Reset-polarity disagreement, classified three ways
*Topic: Semantics/Policy · Disposition: DECIDE (one wording for docs and paper)*

- **Branch** (`PHASE6_PERF.md` §34.6, `BLOCKER_HANDOFF.md`): the exporter defect
  that caps coverage at 8.
- **Ledger §46.2:** a pre-D2 export shape that does not affect agreement. master
  admits 8 sequential blocks.
- **D2:** trusted deliberately.

### C5. Two support answers
*Topic: Policy · Disposition: KEEP-MASTER*

The scalar `SupportedByProjection` is unchanged: `memFree`, plus
`OpSupported` with 17 operators. master states a second admission
(`admitsB`/`admitsWB`) beside it, with two separate reports.

### C6. What an FP "YES" means
*Topic: Coverage · Disposition: DECIDE (harmonize for the paper)*

- **Branch:** 6 stimuli, a trace and a control interpreter, on the covered
  backend at native -O2.
- **master's matrix:** generation plus readiness, interpreted, with `hproj`
  trusted. Only B11 and B12 also carry a COMPLETE reference comparison.

### C7. Memory representation and admission
*Topic: Semantics · Disposition: RECORD*

- The reference `interpretDesign` memory is a total function. The FP memory route
  uses finite word lists, walked on every read.
- An enabled write to an out-of-range address is refused at admission.
- Raw input widths are not checked; stored flop widths are.
- `initStateOf` is a legal-state witness, not an RTL startup contract.

### C8. Front-end toolchain
*Topic: Upstream · Disposition: RECORD*

- **O1 pipeline:** the old one was import/cprop/formal; master's adds
  bitwidth/legalize/enableopt.
- **yosys:** 0.60 on the branch, snapshot 435977e9 on master.
- **B01:** passed the old frontend and was refused by master's; it has since
  moved further on master.

Evidence: ledger §63.27, §54.5.

### C9. Evidence capture
*Topic: Workflow · Disposition: KEEP-MASTER*

The branch records experiments with `record_experiment.py` prelaunch/record
into `experiments.jsonl`. master uses `fp_replay.py`, with immutable attempts
and content-bound identity.

---

## D. Open on both sides

### D1. `hproj` is trusted
`mixDriver` does not kernel-reduce at nontrivial fuel, so each per-design run is
a native execution.

### D2. `mixProgram_implements_mixHost` is unproved
The second projection agrees with the object-language `mixProgram` only.

### D3. Partial audit coverage
The primary `Audit` transitively covers 21 of the 38 pinned modules.

### D4. S3 is unproved
Only step 1 of its bridge is done (A11).

### D5. Eight FP cells are BLOCKED upstream of certificate emission
- **B01:** combinational cycle from a memory read to that memory's write
  address. The cut-vs-loop decision has not been taken.
- **B02, B06, B07, B08:** combinational loops.
- **B04:** compiles since b9b404981, but its FP row has not been re-run.
- **B03:** emits DCERT2 (956,571 B) but has not been run through the memory
  route.
- **B05:** slang use-before-declaration.

### D6. Operators outside `OpSupported` on both sides
`Op_Mult`, `Op_Div`/`Op_UDiv`/`Op_SDiv` and `Op_SetMask`, among others
(`Projection/HardwareInterpreter.lean:938` ✓).

- Rem is refused by master's scan.
- Div maps to UDiv regardless of sign, the inherited `-32/3` issue (plan §14.7).
- On the frozen 29: 7 refused on `ops`; `aes` unsupported.

### D7. Undecided sequential blocks
Six historical DCERT1 sequential blocks are undecided at the cap
(`issue_read_operands` at 7200 s). Fresh `pmp_data_if` emission returns `rc=6`.

### D8. Final acceptance not run
The §6.4 final acceptance has not been run: generated simulator source compared
byte for byte, plus 30 raw-source comparisons. The low-memory runner (plan
§15.7) is unimplemented.

---

## Decisions and fixes still pending

**FIX-MASTER**, each a small change, none started:
- **B3:** add the `memWrite` (34) entry to `MixProgram.primTable`.
- **B4:** update or retire `check_overlay.sh`'s expected hash, with a
  rationale.
- **B5:** set redacted defaults that resolve in place.
- **B12:** doc and metadata corrections.

**DECIDE:**
- **B1:** a DCERT2 loader inside the Projection modules.
- **B2:** a native DCERT2 route.
- **C3:** which edge-vector rule.
- **C4:** reset-polarity wording.
- **C6:** what an FP "YES" means.

## Reproducing the file-level counts

```bash
files=$(git diff --name-only bc9fcb1ba 9bb41a09d)          # 94
git diff --name-status 9bb41a09d HEAD -- $files | cut -f1 | sort | uniq -c
# expect: 33 M, no D  ->  61 byte-identical
```

The ✓ rows were re-checked as follows:
- `grep memWrite Projection/MixProgram.lean` (B3);
- `check_overlay.sh:26` against `sha256sum Translation/LGraphModel.lean` (B4);
- the `LGraphModel` diff (A7);
- `HardwareInterpreter.lean:938` (D6);
- `emit_dcert.cpp:143`, `certificate_ir.cpp:74,345` and `CertLoad.lean:23,187,200` (A3, A5, B1);
- `CertIORoundTrip.lean:10-14,1046` (A4, B12);
- ledger lines 4321-4329, 5009-5010, 5014-5030 and 5866-5872 (B6, A14, A15, B9).
