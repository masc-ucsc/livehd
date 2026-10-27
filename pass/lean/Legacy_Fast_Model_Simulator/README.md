# Legacy Fast Model Simulator

The **legacy** route of `pass.lean`.  A C++ exporter reads an LGraph and writes
one self-contained Lean program per design: an executable model of the hardware,
a certificate describing the same graph as data, and the proofs relating them.
Everything is generated text — there is no hand-written Lean per design.

It is called *legacy* because it predates the verified-compiler route
(`mode=verified_compiler`), which emits only a `DesignCert` and lets a
once-and-for-all-proved compiler produce the model.  Legacy is still the default
`formal.lean.mode`.

What is particular to it is that the emitted file runs on its own: the model is
in the file, so no separate simulator has to read a certificate first, and
`lake` links it to a native binary.  It is **not** the only route that produces
a running binary — the Futamura projection builds native `-O2` drivers and the
IR-as-semantics simulator builds `lgraph-sim`.  What `verified_compiler` mode
does not emit is an executable model; it emits only the `DesignCert` those
routes read.

---

## 1. Build

### The exporter

```bash
bazel build -c opt //lhd:lhd
```

### The Lean support library

```bash
cd formal/lean
lake build LeanSemanticPrimitives LeanSemanticPrimitives.Translation.OpBridge
```

**Name `OpBridge` explicitly.**  The library root deliberately does not import
it (it pulls all of Mathlib), so a bare `lake build LeanSemanticPrimitives`
leaves `OpBridge.olean` absent and every generated file then fails in under a
second with `object file '...OpBridge.olean' ... does not exist`.  That error
looks like a proof failure and is not one.

### Toolchain

`formal/lean/lean-toolchain` pins **Lean ≥ 4.36.0**.  Older toolchains work for
narrow designs but cannot compile a state record wider than 255 fields — see
§5.  Mathlib binaries come from the upstream cache (`lake update` fetches them);
a from-source Mathlib build is not required.

---

## 2. How it works

```
RTL ──► s1_compile ──► LGraph ──► s2_single_edge ──► pass.lean (legacy)
                                                        │
                                          <Top>_Lgraph.lean + <Top>_io.json
```

The emitted file contains **two presentations of one intermediate
representation**, in this order:

| what | declarations | role |
|---|---|---|
| records | `<Top>_in`, `<Top>_out`, `<Top>_state` | one state field per flop, plus one per non-ROM memory and one per synchronous read port |
| **fast model** | `<Top>_comb`, `<Top>_next`, `<Top>_step` | direct `BitVec` computation. This is what *executes*. |
| **certificate** | `<Top>_nodeCerts`, `<Top>_sourceEnv`, `<Top>_graphCert` | the graph itself as data: nodes, operations, dependencies |
| projections | `<Top>_outputsFromCert`, `<Top>_nextStateFromCert` | read outputs / next state out of an evaluated environment |
| cert model | `<Top>_comb_cert`, `<Top>_next_cert`, `<Top>_step_cert` | `evalGraphC` applied to the certificate |
| theorems | four, see §3 | |

Why two: the fast model is fast because it is ordinary Lean code with no
interpreter in the loop.  The certificate is slow but is *the graph as data*,
and it is interpreted by `evalGraphC` from the support library — code shared by
every design and not generated per design.  Agreement between the two is the
property worth proving.

**Both are rendered from one `CertificateIR`.**  `scan_design` reads the LGraph,
`build_certificate` turns that scan into a `CertificateIR`, and then both
emitters walk the same node list: `emit_legacy_fast_model` lowers each node to a
Lean expression (`lower_fast_expr`), `emit_legacy_graph_cert` writes the same
node out as data.  They are independent *lowerings*, not independent readings of
the hardware — §3 says what that costs.

**The certificate is not a separate file.**  Everything in the table above lands
in the one `<Top>_Lgraph.lean`, and it is on by default: `formal.lean.emit_cert`
defaults to `true`, so the explicit `--set` in §4 and §8 is redundant and kept
only to make the runs self-describing.  `emit_cert=false` emits the fast model
alone — no certificate, no theorems, nothing to check.

`<Top>_io.json` carries input/output ordinals, widths, clocks, and every flop's
name in runtime order.  It is built from the same scan, so it cannot drift from
the Lean file — but note the Lean field names are escaped (`ptw_req_data_q[35:0]`
becomes `st_ptw_req_data_q_x5b_35_x3a_0_x5d_`), so tools that need field names
must read the Lean file, not the sidecar.

---

## 3. Trust boundary

**Read this before quoting a row as "proven".**

### Per design, not once and for all

Everything here is generated per design, **the proofs included**.  There is no
theorem that the exporter is correct.  There is a fresh proof, per design, that
two renderings of that design's IR agree — and it has to be rechecked from
scratch whenever the design, the emitter, or the support library changes.

The *checking* is formal: Lean's kernel checks the proof terms, and
`#print axioms` reports what they rest on.  Nothing here is sampled, simulated,
or spot-checked.  But the *scope* of each result is one design and one emitted
file.  "Per-design machine-checked proof" is the accurate phrase — not "verified
compiler", and not "validation" in the testing sense.  The verified-compiler
route (`mode=verified_compiler`) is the one aiming at a once-and-for-all
theorem; legacy trades that away for running on any design the scan accepts,
today.

**The exporter is inside the trusted base.**  Both Lean models come from one
`CertificateIR` (§2), so a C++ bug in `scan_design` or `build_certificate`
reaches both of them identically, and every theorem still proves.  Per-design
proving bounds what the *lowering* can get wrong; it does not bound what the
*reading of the graph* got wrong.

### What a default run establishes

With `emit_cert=true` (and `emit_fast_bridge` left at its default `false`), the
emitted theorems are:

```lean
theorem <Top>_comb_cert_refines_cert   : <Top>_comb_cert i s = <Top>_outputsFromCert (evalGraphC …) := rfl
theorem <Top>_next_cert_refines_cert   : <Top>_next_cert i s = <Top>_nextStateFromCert (evalGraphC …) s := rfl
theorem <Top>_step_cert_eq             : <Top>_step_cert i s = (<Top>_next_cert i s, <Top>_comb_cert i s) := rfl
theorem <Top>_step_cert_refines_lgraph_certificate : … := rfl
```

**All four are `rfl`, and each unfolds a definition written immediately above
it.**  They are definitional tautologies.  The executable fast model
(`<Top>_comb`, `<Top>_next`, `<Top>_step`) appears in **none** of them.

That is not worthless — getting these to elaborate means the certificate
literal is well-typed, `evalGraphC` typechecks against it, the projections have
the right shapes, and every `BitVec` width lines up across a multi-megabyte
file.  That catches real emission bugs.  But it is a **well-formedness result,
not an equivalence result**, and a green axiom audit on these four theorems does
not say the thing you run matches the certificate.

### What connects the executable to the certificate

`--set formal.lean.emit_fast_bridge=true` additionally emits

```lean
theorem <Top>_comb_refines_fast : <Top>_comb i s = <Top>_comb_cert i s
theorem <Top>_next_refines_fast : <Top>_next i s = <Top>_next_cert i s
theorem <Top>_step_refines_fast : <Top>_step i s = <Top>_step_cert i s
```

These are the real content.  **Rows measured without the bridge are not
certified executables**; they are executables plus a separately checked
certificate.  Say which you mean.

### What this route never proves, under any option

- **That the LGraph matches the RTL.**  That is the LEC gate, a separate flow.
  Everything here is downstream of the graph and inherits whatever the graph got
  wrong.
- **That the scan read the graph correctly.**  Both models are rendered from one
  `CertificateIR` (§2).  Anything `scan_design` or `build_certificate` gets wrong
  is present in both, so the bridge theorems of the previous section still prove.
  What the bridge compares is the *lowering* (`lower_fast_expr`, generated C++)
  against the *interpreter* (`evalGraphC`, library Lean) — neither against the
  hardware.
- **That `evalGraphC` / `eval_op` correctly model LGraph semantics.**  The
  certificate interpreter is an *independent re-implementation* of the operator
  semantics, and its value as a cross-check rests entirely on that independence.
  Nothing mechanically enforces it today: if the interpreter were ever
  "simplified" to call the same primitives the fast model uses, the bridge
  theorems would become tautologies and still pass.
- **The Lean compiler and runtime**, for anything you execute.  Proofs are
  checked by the kernel; a running binary additionally trusts code generation.

### Axioms

A clean audit prints `[propext]` and nothing else.  Treat `sorryAx` as a
failure, and `ofReduceBool` as a declaration that the result rests on compiled
code (`native_decide`) rather than on the kernel alone.

```bash
# append to a copy of the generated file, then elaborate that file
#print axioms <Top>_Lgraph.<Top>_comb_cert_refines_cert
```

Qualify with the namespace: the bare name is an unknown constant.

### Exit code is not a verdict

Above the constructor field limit (§5) on an older toolchain, the file **exits
non-zero while its theorems are still proved and axiom-clean** — the compiler
rejects the inductive, the kernel does not.  A gate keyed on exit status scores
such a row FAIL and is wrong about why.  Gate on the axiom audit.

---

## 4. How to run a design

### Emit

```bash
lhd compile lg:<lgraph-dir> --top <Top> \
    --emit-dir lean:<out> \
    --set formal.lean.emit_cert=true
# add --set formal.lean.emit_fast_bridge=true for the equivalence theorems (§3)
```

There is no width cap by default; `--set formal.lean.max_width=N` imposes one.

### Check it

Copy `<Top>_Lgraph.lean` into `formal/lean/LegacySims/` (gitignored), append the
`#print axioms` lines, and elaborate that one file:

```bash
cd formal/lean && lake env lean LegacySims/<Top>_Lgraph.lean
```

Run `lake build` first.  `lake env` performs **no build and no trace check** —
it elaborates against whatever `.olean` happens to be on `LEAN_PATH`, so without
a prior build a run can silently typecheck against a stale library and report
green.

### Make it executable

```bash
python3 pass/lean/Legacy_Fast_Model_Simulator/scripts/make_legacy_sim.py \
        <out>/<Top>_Lgraph.lean --cycles 8 \
        --out formal/lean/LegacySims --module-prefix LegacySims
```

This writes `<Top>_LegacySim.lean`: a `main` that builds an input record each
cycle, calls `<Top>_step`, and prints the outputs and state.  It emits no
semantics — every value printed comes back from the model.  Field names are
parsed from the Lean file (see §2).

Add a target to `formal/lean/lakefile.toml`:

```toml
[[lean_exe]]
name = "LegacySimMinionTlb"
root = "LegacySims.minion_tlb_LegacySim"
exeName = "legacy-sim-minion-tlb"
```

then

```bash
cd formal/lean && lake build LegacySimMinionTlb && ./.lake/build/bin/legacy-sim-minion-tlb
```

Build through lake, not `leanc` by hand: the generated file imports `OpBridge`,
so a hand-rolled link fails on `initialize_…_OpBridge` / `initialize_mathlib_Mathlib`.

The default stimulus is a generic counter, enough to show the model computing.
Driving real vectors means replacing one function, `stim`, in the generated
driver.

---

## 5. The constructor field limit

The state record holds one field per flop.  A Lean constructor could hold at
most **255 object fields**: `lean_object` stores the count in an 8-bit header
field (`m_other:8`, `include/lean/lean.h`).

**Lifted in Lean 4.36.0** (lean4 PR #15516): the compiler keeps the first 254
object fields in the constructor and chains the surplus through spill objects,
costing one pointer hop per 254 fields.  Measured here on v4.36.0-rc1: 409,
1011 and 2403 fields all compile and run.  Very wide records additionally need
`set_option maxRecDepth` and `set_option maxHeartbeats 0`.

### It is a sequence of limits, not one wall

Lifting the field cap moves the constraint downstream rather than removing it.
Taking `issue_stage_gate` (334 fields, 4.2 MB of Lean) all the way to a running
binary hit four in turn, and **none of them was a hard limit**:

| # | limit | enforced by | resolution |
|---|---|---|---|
| 1 | 255 constructor object fields | Lean compiler | Lean ≥ 4.36.0 |
| 2 | 2048 brace nesting in the generated C | **clang** | `-fbracket-depth` |
| 3 | `maxRecDepth`, elaborating the *driver* | Lean elaborator | `set_option` |
| 4 | `maxHeartbeats` | Lean elaborator | `set_option` |

Limits 2–4 are already handled in this repo and need no action:

- `formal/lean/lakefile.toml` gives the `LegacySims` library
  `moreLeancArgs = ["-fbracket-depth=32768"]`.  A real fast model lowers to
  deeply nested C — `issue_stage_gate` emits **126 MB** of it, nested **6736**
  braces deep — and clang's default stops at 2048.  Expect
  `-Wstack-exhausted` warnings from clang at that depth; they are warnings.
- `scripts/make_legacy_sim.py` emits `set_option maxRecDepth 100000` and
  `maxHeartbeats 0` into every driver.  A wide input or state record is one
  term with an entry per port or flop, and the stock limits are sized for
  ordinary source.  Without them a large row fails *while elaborating the
  driver* — the last and smallest file, long after the model compiled — which
  points nowhere near the cause.

### Two traps

- **Exactly 256 fields on Lean < 4.35.0** is accepted, the count wraps to 0, and
  every field leaks on free (lean4 issue #15527).  Silent.  Use ≥ 4.35.0.
- Field counts are inflated by bit-slicing, not by real state.  yosys-slang
  splits one RTL register into several flops, so `frontend_gate`'s 409 fields
  are 31 distinct registers.  Since 4.36.0 coalescing them is no longer needed
  to execute, and §7 shows it would not speed up *proving* either — but it
  would flatten the C nesting behind limit 2, which is the one still producing
  compiler warnings.

---

## 6. Benchmarks covered

Twelve designs, CVA6 and CORE-ET blocks plus a small RISC-V core.  Emission is
measured on all eleven rows that have a usable LGraph; `vpu_top` fails upstream
in `s1_compile` (combinational loop) and never reaches any route.

| row | top | family | state fields | emitted bytes | emits |
|---|---|---|---:|---:|:--:|
| B01 | `minion_dcache_top` | CORE-ET | 1011 | 10,379,732 | yes |
| B02 | `cva6_hpdcache_subsystem_gate` | CVA6 | 554 | 11,074,333 | yes |
| B03 | `icache_top` | CORE-ET | 516 | 11,651,792 | yes |
| B04 | `cva6_mmu_gate` | CVA6 | 740 | 5,204,968 | yes |
| B05 | `shirecache_mesh_master` | CORE-ET | 96 | 5,217,785 | yes |
| B06 | `load_store_unit_gate` | CVA6 | 876 | 7,241,736 | yes |
| B07 | `vpu_top` | CORE-ET | — | — | blocked upstream |
| B08 | `intpipe_top` | CORE-ET | 2403 | 12,597,259 | yes |
| B09 | `issue_stage_gate` | CVA6 | 334 | 4,187,286 | yes |
| B10 | `frontend_gate` | CVA6 | 409 | 3,188,984 | yes |
| B11 | `minion_tlb` | CORE-ET | 31 | 186,051 | yes |
| B12 | `picorv32` | RISC-V core | 175 | 903,531 | yes |

**The emitter accepts every row that has a graph.**  Elaboration and the axiom
audit are a separate, much longer step and have been completed on a subset; see
the table in §7.  Rows not listed there are **not measured**, which is not the
same as failing.

Every byte count above is one `<Top>_Lgraph.lean` holding the fast model, the
certificate, and the four well-formedness theorems (§2).  **No row has been
emitted with `emit_fast_bridge=true`**, so no row in this table or in §7 carries
a proof that the executable matches the certificate — see §3 before quoting any
of them.

---

## 7. Performance

Wall-clock for emission, and for elaboration plus axiom audit of the four
well-formedness theorems (§3).  Measured on Lean v4.36.0-rc1 unless noted.

| row | fields | bytes | emit | elaborate + audit | peak RSS | axioms |
|---|---:|---:|---:|---:|---:|---|
| B11 `minion_tlb` | 31 | 186 KB | < 1 s | **13 s** | 6.6 GB | `[propext]` |
| B12 `picorv32` | 175 | 904 KB | < 1 s | 71 s *(v4.31.0)* | — | `[propext]` |
| B10 `frontend_gate` | 409 | 3.1 MB | 1 s | **1007 s** | 3.2 GB | `[propext]` |
| B09 `issue_stage_gate` | 334 | 4.2 MB | < 1 s | **1559 s** | 3.8 GB | `[propext]` |
| B05 `shirecache_mesh_master` | 96 | 5.2 MB | ~1 s | 1812 s *(v4.31.0)* | — | `[propext]` |

Five of the twelve rows have been checked.  The other seven emit (§6) and have
**not** been elaborated; that is unmeasured, not failing.

Two things the numbers show:

- **Cost tracks the file, not the field count.**  B09 and B10 settle this on
  one toolchain: B10 has *more* state fields (409 vs 334) and is the *faster*
  of the two (1007 s vs 1559 s), tracking emitted size (3.1 MB vs 4.2 MB)
  rather than field count.  Field count is what the constructor limit (§5)
  measures and what drives *code generation*; file size drives *elaboration*.
  So coalescing bit-slices (§5) buys code-generation speed, not proof speed.
- **4.36.0 is faster than 4.31.0 on the same input.**  B11 went from 30.2 s to
  13 s, a 2.3× improvement, with byte-identical emitted text.

Executables, both built and run on v4.36.0-rc1:

| row | fields | binary | runs |
|---|---:|---:|---|
| `minion_tlb` | 31 | 169.9 MB | yes, exit 0 |
| `issue_stage_gate` | 334 | 7.7 MB | yes, exit 0 |

**Binary size is not a function of design size** — the larger design produced
the smaller binary by a factor of 22.  The difference is in how much of the
imported Mathlib-derived runtime each link pulled in (their linker response
files are 1.36 MB and 1.8 KB of object paths respectively); the cause has not
been run down, so do not read either number as characteristic.

`minion_tlb` was 163 MB on v4.31.0 against 169.9 MB here: the 3.9 % growth is
the spill-object support from §5, and matches the +3.99 % output size reported
on lean4 PR #15516.

---

## 8. Two worked examples

### Simple — `minion_tlb` (B11, CORE-ET, 31 state fields)

The whole route end to end, in about fifteen seconds of compute.

```bash
# 1. emit
lhd compile lg:<B11-lgraph> --top minion_tlb \
    --emit-dir lean:out/B11 --set formal.lean.emit_cert=true
#   -> out/B11/minion_tlb_Lgraph.lean   186,051 bytes, 31 state fields

# 2. check
cp out/B11/minion_tlb_Lgraph.lean formal/lean/LegacySims/
cd formal/lean && lake build LeanSemanticPrimitives.Translation.OpBridge
lake env lean LegacySims/minion_tlb_Lgraph.lean      # 13 s, exit 0

# 3. run
python3 pass/lean/Legacy_Fast_Model_Simulator/scripts/make_legacy_sim.py \
        out/B11/minion_tlb_Lgraph.lean --out formal/lean/LegacySims \
        --module-prefix LegacySims
cd formal/lean && lake build LegacySimMinionTlb
./.lake/build/bin/legacy-sim-minion-tlb
```

```
top minion_tlb cycles 8 inputs 14 outputs 5 state 31
cycle 0 flop 0 0 ... 0
cycle 1 flop 0 0 ... 1
cycle 2 flop 0 0 ... 0
```

The last flop toggles with the clock, which is how you know the model is
computing rather than returning a constant.  Outputs stay zero because the
counter stimulus never presents the TLB a valid request.

Its state record shows the bit-slicing from §5 directly — four fields, one
register:

```lean
st_ptw_req_data_q_x5b_35_x3a_0_x5d_  : BitVec 36   -- ptw_req_data_q[35:0]
st_ptw_req_data_q_x5b_37_x3a_36_x5d_ : BitVec 2    -- ptw_req_data_q[37:36]
st_ptw_req_data_q_x5b_65_x3a_38_x5d_ : BitVec 28   -- ptw_req_data_q[65:38]
st_ptw_req_data_q_x5b_69_x3a_66_x5d_ : BitVec 4    -- ptw_req_data_q[69:66]
```

### Complicated — `issue_stage_gate` (B09, CVA6, 334 state fields)

CVA6's issue stage and scoreboard: 4.2 MB of generated Lean, 32 inputs, 40
outputs, and a state record **wider than a Lean constructor could hold before
4.36.0**.  On the pinned v4.31.0 this row could be proved but never compiled;
it is the reason the toolchain moved.

```bash
lhd compile lg:<B09-lgraph> --top issue_stage_gate \
    --emit-dir lean:out/B09 --set formal.lean.emit_cert=true
#   -> 4,187,286 bytes, 334 state fields

cp out/B09/issue_stage_gate_Lgraph.lean formal/lean/LegacySims/
cd formal/lean && lake env lean LegacySims/issue_stage_gate_Lgraph.lean
```

```
exit 0, 1559 s, 3.8 GB peak
'issue_stage_gate_Lgraph.issue_stage_gate_comb_cert_refines_cert'   depends on axioms: [propext]
'issue_stage_gate_Lgraph.issue_stage_gate_next_cert_refines_cert'   depends on axioms: [propext]
'issue_stage_gate_Lgraph.issue_stage_gate_step_cert_eq'             depends on axioms: [propext]
'issue_stage_gate_Lgraph.issue_stage_gate_step_cert_refines_lgraph_certificate' depends on axioms: [propext]
```

The driver generator reports the width rather than failing late:

```
top=issue_stage_gate inputs=32 outputs=40 state_fields=334 cycles=8
  NOTE: 334 state fields exceeds the 255 object-field limit of a
        Lean constructor.  Needs Lean >= 4.36.0 …
```

It also builds and runs, via the `LegacySimIssueStage` target:

```bash
cd formal/lean && lake build LegacySimIssueStage
./.lake/build/bin/legacy-sim-issue-stage
```

```
top issue_stage_gate cycles 8 inputs 32 outputs 40 state 334
cycle 0 out … 2 … 176 0 2066035336255469781080 0 0 119
cycle 1 out … 3 … 192 0 2213609288845146194016 0 0 130
cycle 2 out … 4 … 208 0 2361183241434822606952 0 0 141
cycle 1 flop … 22 …
```

Unlike the TLB above, the outputs are non-zero and advance cycle to cycle
(2→3→4, 176→192→208, 119→130→141), so the model is visibly computing on the
counter stimulus rather than sitting in reset.

Getting here needed the §5 settings — clang's bracket depth and the driver's
`maxRecDepth` — both of which are now in the repo.  On v4.31.0 none of this was
reachable: the state record could not be compiled at all.

Per §3, both examples are checked **without** the fast-view bridge, so what is
established is certificate well-formedness, not fast-model equivalence.  Add
`--set formal.lean.emit_fast_bridge=true` for that.

---

## 9. Code layout

Everything that belongs to this route and to nothing else is in this folder.
Three things it needs are deliberately **not** here, because other routes use
them too.

### This folder

| path | role |
|---|---|
| `legacy_model.{cpp,hpp}` | coordinates the emitters below |
| `emit_legacy_schema.*` | `_in`/`_out`/`_state` records, RTL mapping comments |
| `emit_legacy_fast_model.*` | the fast model, `lower_fast_expr` |
| `emit_legacy_graph_cert.*` | sparse-id `GraphCert`, source environment, projections |
| `emit_legacy_cert_wf.*` | optional chunked well-formedness proofs (`cert_wf`) |
| `emit_legacy_fast_bridge.*` | optional per-node bridge — the §3 equivalence theorems |
| `legacy_emit_options.hpp` | `LegacyEmitOptions`, split out to break a header cycle |
| `legacy_model_test.cpp` | the unit tests (`//pass/lean:legacy_model_test`) |
| `scripts/make_legacy_sim.py` | driver generator: the `main` that runs a design (§4) |
| `scripts/nary_bridge_check.sh` | elaborates 14 fresh n-ary / Or bridge fixtures |
| `tests/legacy_fixtures.cpp` | fixture-graph generator (`//pass/lean:lean_legacy_fixtures`) |
| `tests/replay_legacy_proofs.py` | serial corpus replay: time, RSS, axiom audit |
| `tests/check_legacy_oracles.py` | tiny legacy exports against `DesignCert`, sampled |
| `tests/audit_legacy_parity.py` | baseline-vs-refactored export comparison |
| `tests/legacy_semantic_audit/` | the Lean audits behind specific semantic decisions |
| `tests/LEGACY_*`, `tests/MASTER_*` | recorded runs: coverage, scalability, bridge and record-layout results |

Bazel targets keep their old labels in `pass/lean/BUILD`; only the `srcs` paths
name this folder.

### What this route uses but does not own

| what | where | why it is not here |
|---|---|---|
| the exporter front half | `pass/lean/Exporter/Frontend/` | every route uses it: pass entry, options, `scan_design`, `build_certificate`, memory lowering, the `_io.json` sidecar |
| the Lean support library | `formal/lean/LeanSemanticPrimitives/` | `evalGraphC`, `OpBridge`, `NaryBridge`, `OrBridge`, `LegacyCertWF` — shared, not generated per design |
| shared gates | `pass/lean/scripts/` | `op_census.py`, `const_parity.py` and `axiom_audit.py` with their controls; three routes and the repo-level runners call them |
| the 12×5 coverage harness | `pass/lean/scripts/` | `fp_cov_run.sh` and the `fp_cov_queue_*.sh` queues both take `legacy` or `verified_compiler` |

`Exporter/DesignCert/` is the one exporter part this route does **not** touch;
it writes the `DesignCert` the other three routes read.

Options are documented in [`../README.md`](../README.md#options); the scan and
certificate stages this route builds on are described in the same file.
