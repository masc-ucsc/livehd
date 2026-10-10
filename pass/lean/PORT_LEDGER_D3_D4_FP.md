# Contribution and procedure ledger: D3, D4, and the Futamura projection

Opened while the CORE-ET/CVA6 queues drain. **Analysis and staging only** — no
shared Lean library was rebuilt, nothing was activated, and the live replay's
library context (R31.3) is unchanged. Working notes are under
`generated/port_staging/` (untracked); this file is the ledger.

## 0. Sources, pinned

| branch | path | ref | revision | date | files in scope |
| --- | --- | --- | --- | --- | ---: |
| D3 | `livehd-d3-translation-validation` | `direction-3-translation-validation` | `78a3e0d8c10c3e1ce1627d8d54fa898ee4416d43` | 2026-10-04 | 300 |
| D4 | `livehd-d4-incremental` | `direction-4-incremental` | `f6ad1fe29f8f15aeda045115864fd81a34c33aef` | 2026-10-04 | 356 |
| FP | `livehd-futamura/livehd` | `projection` | `9bb41a09d6b57111ed2b3fd10102ec4ff174b6c6` | 2026-10-06 | 295 |

FP additionally pins a **semantic rev** it shares with livehd-new:
`90583be1349acbd25f740b176a406aa8bbe1251c`. Those shared copies are never
edited.

"FP" here is the **Futamura projection**, not floating point: none of the three
branches carries a floating-point semantic surface (`git ls-files` finds only
`docs/papers/LiveHD_IEEE_Micro20.pdf`).

Surveyed against master (`pass/lean`, `formal/lean`, `scripts`, `lhd/tests`):

| branch | new files | shared files that differ | master-only |
| --- | ---: | ---: | ---: |
| D3 | 96 | 140 | 40 |
| D4 | 151 | 143 | 40 |
| FP | 105 | 141 | 40 |

The large `pass_lean.cpp` deltas (2,467–3,863 lines on every branch) are
**master's own refactor**, not branch content: master split that file into
`design_scan`, `lean_format`, `graph_access`, `emit_*`. The semantic surface to
port is the new Lean modules.

## 1. THE BLOCKING INCOMPATIBILITY: D3 and D4 are clockless

This is the first thing to establish, because getting it wrong silently
destroys the post-D2 work.

| branch | `DesignCert.lean` clock schema |
| --- | --- |
| master | `ClockDesc`, `DesignCert.clocks`, `FlopDesc.clock`, `FlopDesc.asyncReset`, `MemoryDesc.clock` |
| D3 | **none** — zero `ClockDesc`/`clocks` lines |
| D4 | **none** — zero; master adds **19** clock-related lines over it |
| FP | **present** — `ClockDesc` at line 108 (it shares the record with livehd-new at a pinned rev) |

**Therefore: D3's and D4's `DesignCert.lean` is never copied into master.**
Their contributions are carried *onto* master's clock-aware record. A port that
moved their file across would revert the multi-clock transport, and every
clock, reset and collision gate built on it, without a single test failing —
the modules would simply stop carrying clocks.

FP's copy is clock-aware **and pinned**: it is shared with livehd-new at a
fixed rev and is not edited here.

## 2. D4 — `CertIO` and the round-trip

**What it adds.** `Compiler/CertIO.lean` (a byte-level writer/parser:
`writeCert`, `renderCert`, `parseCert`, `readSource/readNode/readOutput/
readFlop/readMemory`) and `Compiler/CertIORoundTrip.lean`, whose top-level
obligation is

```lean
theorem parseCert_renderCert (D : DesignCert) : parseCert (renderCert D) = .ok D
```

Both are **`sorry`-free on D4**.

**The incompatibility, exactly.** `CertIO.lean` on D4 contains **zero**
occurrences of `clock`. It serializes sources, nodes, outputs, flops and
memories — and nothing else. So:

- it cannot round-trip master's `DesignCert`, which also carries `clocks`,
  `FlopDesc.clock`, `FlopDesc.asyncReset` and `MemoryDesc.clock`;
- `parseCert_renderCert` as stated on D4 is a theorem about a **different
  record**. Porting the file without extending it would produce a parser that
  silently drops the clock table while its round-trip theorem still typechecks
  over the clockless shape.

**Proof obligations to discharge on master** (none of which exist yet):

1. extend `renderCert`/`parseCert` with the clock table, `FlopDesc.clock`,
   `FlopDesc.asyncReset` and `MemoryDesc.clock`;
2. re-establish `parseCert_renderCert` over the **clock-aware** record;
3. a negative: a byte stream whose flop names a clock ordinal outside `clocks`
   must fail to parse rather than default to 0 — the same refusal
   `cert_shape_check.py` makes statically (R24.1).

## 3. D3 — the `Defs` proof split, and `Reify`

**The `Defs` split is the piece master most wants**, and it is about memory,
not layering. `CompileOpDefs.lean` says so:

> This module holds the EXECUTABLE definitions only, and imports no Mathlib.
> [...] The split exists for MEMORY, not for layering. `import Mathlib` cost
> every sweep probe **6,027,948 kB** of resident set that the probe never used
> — more than the entire design term for most modules. [...] Nothing here may
> acquire a `theorem`: a proof would pull Mathlib back in through the one
> import that is supposed to stay absent.

`CompileOp.lean` keeps exposing definitions *and* theorems, so every existing
importer is unaffected.

| module | lines | `sorry` |
| --- | ---: | ---: |
| `CompileOpDefs.lean` | 100 | 0 |
| `CompileGraphDefs.lean` | 53 | 0 |
| `CompileDesignDefs.lean` | 104 | 0 |
| `Reify.lean` | 197 | 0 |
| `ReifyGen.lean` | 664 | 0 |
| `D3Harness.lean` | 390 | 0 |
| `ReifyProof.lean` | 1,327 | **0** — my count of 4 was comments; see §8 |

`ReifyProof.lean` has **no** holes (§8). What does travel with the port is the
obligation that each **generated** theorem be audited: a failed emission rests
on `sorryAx`, and `d3_proof_gate` / `auditOrThrow` reject it.

**Why this matters here beyond D3.** The standing D3 sweep budget is ~20 GB
aggregate RSS, and the DINO measurements in this ledger run 8.4–13.3 GB per
job. A definition path that does not drag Mathlib in is the difference between
a probe that fits and one that does not. It is also the right carrier for the
direct sweep (R35), whose probes import `DirectTrace` for exactly this reason.

**Port order for D3:** the `Defs` split first (it is mechanical, `sorry`-free
and independently valuable), `Reify`/`ReifyGen` next, then `ReifyProof` with
its three proof emitters and a per-generated-theorem axiom gate.

## 4. FP — the projection (this section was wrong and is corrected)

The first version of this ledger asked "what does `Projection` do that
`Compiler.Direct` does not, rather than porting both". That framing is wrong on
both counts, and the acceptance packet says so directly:

> Actual pinned implementation is **already clock-aware**.
> `specialize_correct_of` / `specializeDesign_correct` / `projectDesign_correct`
> quantify **edges, input, state** as 3 dynamic arguments; `DesignEncoding`
> round-trips flop `clock`/`asyncReset`, memory `clock`, `ClockDesc` names and
> `DesignCert.clocks`. Preserve these exact contributions, rather than imposing
> a clockless prototype story.

So FP is **not** a clockless prototype to be rationalised against `DirectSim`,
and it is not dropped because a simulator already exists. **38 `Projection`
modules** are to be carried, with their support docs, runner, transport and
overlay tools.

### 4.1 Four contracts that must survive the port

**Conditional support, not blanket support.** `projectDesign_correct` is a
fuel-free `Eval` ↔ `ResultRel (interpretDesign)` iff, **conditional on**
specialization success, `SupportedByProjection`, `RuntimeWF` and
`RuntimeSized`. A successful partial evaluation is not a statement that the
hardware is supported. **Memory support stays explicitly refused** until it is
actually proved.

**Host versus object language.** `SecondProjection` proves agreement with the
object-language `mixProgram` **only**. `mixProgram_implements_mixHost` is
documented as a larger **unproved** obligation. Gate0's object-vs-host tests
may **not** be promoted to a universal derived-compiler equality — and the
first and second projection interfaces stay separate.

**An admission-policy difference, not a semantics change.**
`ProjectedStep.runtimeOK` requires `D.clocks.size ≤ edgeVector.size`, while
master's `Direct.checkRuntime` requires the edge vector to match **exactly**
(R-series). That is two retained interfaces disagreeing about admission. It is
preserved and documented as such, not silently reconciled, unless the user
adopts a policy change.

**Axiom discipline.** `PrimaryAudit` carries **311 exact `#print axioms`
obligations**; the universal library proof policy allows standard axioms only,
with **no `native_decide` assumptions**. Prototype paths have their own
`ProtoAudit` obligations and their own status, and `.build-proj.sh` builds **33
targets, not all 38 modules** — a prototype marked blocked or research cannot
be counted as a correct runtime implementation by copying it.

### 4.2 A name collision to resolve

FP's `Compiler.CertIO` shares a module name with **D4's**, and deliberately
drops things D4 keeps. Both cannot occupy that name in one tree. Resolving it
must not lose D4's checked-compiler paths or FP's `asyncOK`/transport
constraints, and the clock-aware version must never silently load older data
with the metadata omitted.

## 5. Procedure

1. **Nothing is activated while a queue is live.** Shared-library changes wait
   for the live handles to become terminal, and their final source and library
   fingerprints are recorded first (R31.3 is the current one).
2. **Carry contributions onto master's record**, never the other way. For D3
   and D4 that specifically means never importing their `DesignCert.lean`.
3. **Each branch keeps its own acceptance.** D3's probe benchmarks, D4's
   round-trip and parser requirements, and FP's conditional support, runtime
   and edge contracts are distinct, and one branch's evidence does not
   discharge another's.
4. **Every generated theorem is audited individually** — the module has no holes, but an emitted proof can still fail onto `sorryAx`, and the gate must reject it.
5. Native LEC / `single_edge` / frontend contracts, the checker/evaluator
   policy, flat records and the pinned Lean/Mathlib environment are unchanged
   throughout.

## 6. Branch-specific acceptance, from the measured packets

Each branch keeps its own acceptance; one branch's evidence discharges none of
another's. Sources:
`automate_workspace/master_rebase/audits/{d3_acceptance,d4_transport_acceptance,futamura_acceptance}_20261010/requirements.json`.

### 6.1 D3

- Build the definition-only modules **without Mathlib** and the proof modules
  **on the current clock-aware D2 schema**; audit `compileDesign_correct` plus
  the guard and bridge theorem axiom sets, rejecting `sorry` and custom
  assumptions.
- D3's runtime calls use the **old two-argument API**; migrate them to explicit
  `allEdges` compatibility or consistently clock-aware APIs **without metadata
  loss**, preserving arbitrary-edge compiler correctness.
- Three boundary repairs are distributed across `LGraphModel`,
  `ResidualSemantics` and `OpBridge` and must be preserved with their proofs
  and their independent source/residual implementations: **result-width SHL
  saturation, source-width SRA saturation, strict-greater Sext boundary**. The
  permanent boundary matrix includes huge amounts, source/result width
  asymmetry, zero width and strict-equality Sext — **stimulus bounds are not
  shrunk to avoid runtime panics**.
- Simulation "agree" is **sampled evidence**; proof "na" is **unattempted**.
  Both gates and the checker self-test are retained precisely.
- The frozen CVA6-30 names all match the 61-configuration inventory, but that
  is **name coverage only** — not wrapper/source configuration identity, and
  not a replay of D3's procedures.
- D3 scripts/manifests carry historical **private absolute paths**; port to
  project-relative defaults while retaining the logical provenance.

### 6.2 D4

- `parseCert` is **total**, while the header still calls it partial/unverified:
  keep the later total implementation and the round-trip theorem, fix the stale
  documentation.
- **DCERT1 serializes no clocks.** Additive schema defaults do **not** give
  arbitrary-edge metadata fidelity; an explicit format/version disposition is
  required for older DCERT1 data, with **no silent metadata defaulting** for
  edge-aware execution.
- The round-trip theorem promises *every* `DesignCert` but was proved over the
  older schema: the port must **cover every new field rather than narrowing its
  promise unnoticed**, with a full axiom audit.
- The operator decoder uses `Int.toNat` for `Op_Sum` and `Op_MemWriteBE`
  payloads, so **negative payloads normalize to zero**. The malformed-data
  contract is reviewed before any blanket rejection claim: a renderer/parser
  round-trip alone does not establish canonicality on arbitrary input.
- Required evidence also includes **fresh C++ `CertificateIR` vs Lean renderer
  byte/field parity**, malformed/truncated/extra-field/unsupported-version
  controls, whitespace and Unicode in named clocks if the theorem promises
  every `String`, owned publication failure/rollback/pair-coherence tests, and
  D4's own runtime/scale benchmarks.

### 6.3 FP

See §4. In addition: port all 38 module contributions plus the retained
support docs, runner, transport and overlay tools, recording intentional
prototype and deferred status rather than claiming all promoted; run the
retained first-projection fixture, trace, scaling and DCERT runner procedures
on **fresh** transport exports with forced observable output, state checksum
and value comparisons, measuring wall and RSS; validate
clock-independent/quiet/simultaneous/reset edges on the projected step; and
cross-compare checked projected/direct/compiler results **only where all their
admission predicates hold**.

## 7. First migration step, staged

The D3 `Defs` split is **derived, not copied** —
`generated/port_staging/derive_defs_split.py` regenerates it from master's
current clock-aware modules:

| module | definitions moved | theorems retained | scaffold lines kept in both |
| --- | ---: | ---: | ---: |
| `CompileOp` | 1 | 28 | 6 |
| `CompileGraph` | 3 | 6 | 3 |
| `CompileDesign` | 7 | 11 | 3 |

Zero theorems leaked into the definition modules, which is the invariant the
split exists to maintain. The output is **staged, not installed, and not
elaborated** — elaboration writes oleans into the live shared library context
that a running queue depends on.

Copying D3's own `*Defs.lean` would have been the mistake the packet names:
*"`compileFlop` and `compileMemory` lack clock/asyncReset; `ResidualProgram`
construction lacks clocks. This is older schema, not an acceptable replacement
of current post-D2 definitions."*

## 8. A claim I made that was false: ReifyProof's "four holes"

This section said `ReifyProof.lean` carried **4 `sorry`s in 1,327 lines**, and
that count was used to defer the whole module and two proof fixtures.

**It is wrong.** Verified against the pinned blob
(`e6e05b4f1d92f6bc5f7dda19a627105f2fc7c43bf94c28cea68fa4cb7b9b5537`): the four
matches are **prose inside doc comments**, at lines 79, 225, 1281 and 1306 —
three of them describing the gate that *rejects* `sorryAx`:

```
79:  /-- The axioms a D3 proof may depend on.  Anything else -- `sorryAx` from an
225: theorem resting on `sorryAx`. The gate downstream caught it -- so nothing was
1281:`#print axioms` only prints; a probe that merely printed `sorryAx` would still
1306:`proof=1` for a theorem resting on `sorryAx`.  Here the print is in the same
```

Stripping comments and string literals leaves **zero** `sorry`, `admit`,
`sorryAx`, `mkSorry`, `native_decide` and **zero** axiom declarations across
161 lines of code.

**Where it came from.** `grep -c 'sorry'` — a substring count over the whole
file, comments included — reported as proof holes. The same lexical-for-semantic
substitution this ledger keeps recording in other people's code, in mine, used
to justify not porting a module.

**The module builds unmodified**, exit 0, from the D3 pin. It is required branch
content, not research-only, and the two fixtures that import it are not
excused. The `BLOCKED` exception in `run_probes.sh` that rested on this count
has been removed.

What remains true, and is a different claim: a **generated** proof can still
fail and introduce `sorryAx`, which is exactly what `d3_proof_gate` and
`auditOrThrow` exist to reject. Per-generated-theorem axiom decisions are
required and are not weakened.

## 9. Status

| item | state |
| --- | --- |
| sources located, revisions pinned | done |
| contribution survey vs master | done |
| clock/reset incompatibility (D3, D4 clockless) | done |
| FP already clock-aware — earlier claim corrected | done |
| D4 round-trip and transport obligations | named (§2, §6.2) |
| FP conditional/host-object/admission/axiom contracts | named (§4.1) |
| `Compiler.CertIO` name collision | named, unresolved (§4.2) |
| D3 `Defs` split | **derived from master's sources, staged** (§7) |
| anything elaborated or installed | **no — the live library is untouched** |
| activation | waits for the queue handles to be terminal, with final fingerprints preserved |

## 10. D3 `Defs` split: built and audited in isolation

The split is no longer "staged, unverified". All six modules compile under the
pinned 4.31.0 toolchain in an isolated prefix, and the public interface is
proved unchanged.

### 10.1 The build

| module | exit | wall | peak RSS |
| --- | ---: | ---: | ---: |
| `CompileOpDefs` | 0 | 1.17 s | 489,628 kB |
| `CompileGraphDefs` | 0 | 0.81 s | 483,056 kB |
| `CompileDesignDefs` | 0 | 0.85 s | 483,544 kB |
| `CompileOp` (wrapper) | 0 | 20.81 s | 6,773,196 kB |
| `CompileGraph` (wrapper) | 0 | 19.42 s | 6,726,636 kB |
| `CompileDesign` (wrapper) | 0 | 12.93 s | 6,757,984 kB |

One warning, `This simp argument is unused: hc` in `CompileDesign` at 283:26 —
present in the source and not introduced by the split.

**~480 MB without Mathlib against ~6.75 GB with it: 14×.** D3's own measurement
was 6,027,948 kB per probe; this reproduces it on master's clock-aware sources,
which is the entire reason the split is being carried. Against the standing
~20 GB aggregate sweep budget, that is the difference between a probe that fits
and one that does not.

### 10.2 The interface is unchanged — audited, not asserted

A generated probe `#check @…` and `#print axioms …` for **all 45 public
theorems** of the three modules, run twice: once against the isolated split,
once against the live modules. The two outputs are **byte-identical**.

| axiom set | theorems |
| --- | ---: |
| `[propext]` | 19 |
| `[propext, Quot.sound]` | 15 |
| `[propext, Classical.choice, Quot.sound]` | 10 |
| **no axioms at all** (`Compiler.Residual.rMaskedUpdateV_correct`) | 1 |

19 + 15 + 10 = 44; the 45th depends on nothing. The first version of this table
left that row out and so did not add up. Standard axioms only. **No `sorryAx`, no `native_decide`.** The first probe
guessed that every theorem lived in `Compiler.*` and five names did not
resolve — the probe was wrong, not the split; it now tracks `namespace`/`end`
and finds them in `Compiler` and `Compiler.Residual`.

### 10.3 The live library is untouched

All **25** recorded `LeanSemanticPrimitives` oleans still match their 16-hex
digests and sizes after every isolated build. `lean` invoked directly writes
only where `-o` points; it is `lake build` that would write the live tree.

### 10.4 What this does not establish

The split is a **memory and layering** change: the same definitions, the same
theorems, the same axioms. It is not new proof content, it does not touch the
clock/reset schema it was derived from, and it has not been installed into the
live tree — activation waits for the replay queues to be terminal and
fingerprinted.

## 11. D4 transport: the exact delta, and the two decisions it forces

Source preparation, from `f6ad1fe29`'s `CertIO.lean` and `CertIORoundTrip.lean`
(both `sorry`-free there, over the **clockless** schema).

### 11.1 What DCERT1 does not carry

```
certBytes D = magicBytes                    -- "DCERT1\n"
           ++ section' srcBytes  D.sources
           ++ section' nodeBytes D.nodes
           ++ section' outBytes  D.outputs
           ++ section' flopBytes D.flops
           ++ section' memBytes  D.memories
```

Measured against master's record, three things are missing and one is absent
entirely:

| master field | DCERT1 |
| --- | --- |
| `DesignCert.clocks : Array ClockDesc` | **no section at all** |
| `FlopDesc.clock : Nat` | not in `flopBody` |
| `FlopDesc.asyncReset : Bool` | not in `flopBody` |
| `MemoryDesc.clock : Nat` | `memBody = aw dw nextImg` — not there |

### 11.2 Decision 1 — a `String` cannot ride in a newline-framed record

`ClockDesc.name` is a `String`, and DCERT1 frames records with spaces and
newlines (`memBody = natBytes aw ++ spc … ++ [chNL]`). A clock named with a
space or a newline would break the framing silently, and the round-trip theorem
promises **every** `DesignCert`.

So a clock name is written **length-prefixed**: its UTF-8 byte count, a space,
then exactly that many bytes. The reader takes the count and then those bytes
without scanning for a delimiter, so no byte sequence in a name can be
mistaken for structure. This is what makes the Unicode and whitespace cases in
D4's required evidence provable rather than merely untested.

### 11.3 Decision 2 — DCERT1 may not silently become a one-clock design

Master's `DesignCert` documents that a certificate which does not spell
`clocks` is the one implicit domain with everything on ordinal 0 — *"exactly
what every certificate emitted before clock provenance existed meant."* That is
a legitimate **schema** default for a record.

A DCERT1 **byte stream** is a different matter: it carries no statement about
whether its design was single-clock. Reading it as one is an assumption about
the *design*, not a default of the record — and that is exactly the silent
metadata defaulting that must not happen for edge-aware execution.

The disposition:

- the magic becomes **`DCERT2`**, and `parseCert` accepts only that;
- a `DCERT1` stream is a **named refusal** from `parseCert`, not an upgrade;
- a separate, deliberately-named entry point reads DCERT1 **with the
  single-clock assumption in its name**, so the assumption cannot be made by
  accident or inherited by a caller that did not ask for it.

### 11.4 A contradiction in the first draft of this section

The first version of §11 asked for **both**:

- `parseCert_renderCert` holding for **every** `DesignCert`; and
- `parseCert` **rejecting** a flop whose clock ordinal is outside `clocks`.

Those cannot both hold. A `DesignCert` with an out-of-range ordinal is a
perfectly good inhabitant of the type, so it is itself a counterexample to the
universal round-trip the moment the parser rejects it. Asking for both is
asking for an inconsistent API, and it would have been discovered only after
the proof failed to close.

**The split.** Syntax and admission are separated:

| layer | promise |
| --- | --- |
| `renderCert` / `parseCert` — the **codec** | `parseCert (renderCert D) = .ok D` for **every** `DesignCert`, unconditionally. It is a faithful encoding of the record, and it judges nothing. |
| `readCertChecked` — **checked admission** | parses, then applies the well-formedness predicate: a clock ordinal outside `clocks`, a slot out of range, a runtime shape that cannot execute. Rejection here is a **semantic** verdict and is named as one. |

So the universal codec theorem keeps its universal promise, the rejection lives
where a rejection belongs, and no API claims both. This mirrors the split
already in the tree: `cert_shape_check.py` refuses statically what
`checkDesign` refuses semantically (R24.1), and `direct_sweep`'s
`PROVENANCE_MISMATCH` is a refusal that does not pretend to be a parse error.

**Which contract applies is recorded before DCERT2 is extended**, not after:
the codec is total and unconditional; the checked read is the only thing that
may refuse on meaning.

### 11.5 Obligations, none of them discharged yet

1. `renderCert`/`parseCert` extended with the clocks section, `FlopDesc.clock`,
   `FlopDesc.asyncReset` and `MemoryDesc.clock`;
2. `parseCert_renderCert` re-established over the **clock-aware** record as the
   **universal codec** theorem — D4's proof is ~900 lines over the clockless
   one, and the promise must not be narrowed to the fields that happen to be
   covered;
3. negatives, all on the **checked read**, never on the codec: a flop naming a
   clock ordinal outside `clocks`; truncated input; extra fields; an
   unsupported version;
4. names with whitespace, multi-byte UTF-8 and an embedded newline must
   round-trip;
5. byte/field parity against a fresh C++ `CertificateIR` serializer;
6. `Int.toNat` on `Op_Sum` and `Op_MemWriteBE` payloads normalizes negatives to
   zero — the malformed-data contract is settled **before** any blanket
   rejection claim;
7. the stale header calling the reader "UNVERIFIED, and therefore trusted" is
   corrected: the implementation below it is already `TOTAL, not partial`.

### 11.6 The `Compiler.CertIO` name collision

D4 and FP both define `Compiler.CertIO`, and FP's deliberately drops what D4
keeps. **Neither is dropped.** They become separate internal modules behind a
facade, so D4's checked-compiler and bounded-slot behaviour and FP's
`asyncOK`/transport constraints both survive under their own names. Resolving
the collision by deleting either side's constraints is not available.

## 12. D3 runtime repairs, carried onto the clock-aware schema

### 12.1 One commit, four guards

All four boundary repairs come from a single D3 commit:

**`9368703518e673db551dd969b1d1b373a3377b44`**, 2026-10-01,
*"formal/lean: guard the wide-shift arms so a huge shift amount cannot kill
Lean"* — 258 insertions, 14 deletions across `ResidualSemantics.lean` (+14),
`LGraphModel.lean` (+54), `OpBridge.lean` (+103) and a 101-line boundary probe.

**Cause, in its own words.** Three CORE-ET modules died at the `reify` gate with
`INTERNAL PANIC: Nat.pow exponent is too big` — `minion_dcache_tensor_load`,
`vpu_ml`, `vpu_tensorquant`. Not wide data paths: 1024/1025-bit widths evaluate
fine. It was raising 2 to a shift **amount**, which is a *value*; those designs
carry 33-bit amount operands, so the exponent reached ~8.6 × 10⁹. The fix is a
guard, **not a narrower stimulus** — narrowing would have hidden the panic while
leaving the semantics able to produce it.

**The three saturation widths are different, and swapping them is a semantic
change rather than a crash fix:**

| op | saturates at | result |
| --- | --- | --- |
| SHL | `k ≥ w`, the **RESULT** width | `0` — `shl_bridge` shifts `(zext a : BitVec w)` |
| SRA | `k ≥ a.width`, the **SOURCE** width | all sign bits: `mk_bv w 0` when `a.width = 0` or `bv_sint a ≥ 0`, else `mk_bv w (-1)` |
| Sext | `n = 0` → `0`; `n > a.width` → `mk_bv w (bv_uint a)` (zero extension) | `n = a.width` is the **equality boundary** and keeps SIGN extension — it is not in the `>` arm |

`sem_sra` shifts at the source width and only then widens, so saturating SRA at
`w` would be wrong wherever `w > a.width`. Value-preservation is proved, not
asserted: `mk_bv_mul_two_pow_ge`, `shl_guard_eq` and `bv_sra_guard_eq`, so
`sra_bridge`, `sra_bridge_sext`, `shl_bridge`, `sext_bridge` and
`sext_bridge_low` keep proving the same statements about the same values.

### 12.2 Carried without the clockless schema

All four are **absent from master**: `k ≥ x.width`, `n > a.width`,
`bv_sra_guard_eq` and both saturation comments return nothing.

The commit's diff applies to master's files **cleanly, with offsets only** — so
the guards are orthogonal to the clock work. After applying, checked
explicitly: master's `inReset && (edge || f.asyncReset)` is still there, and
D3's clockless `if inReset then …` did **not** come along. Diffing the whole
files would have brought it.

### 12.3 Build, interface and boundary evidence

| module | exit | wall | peak RSS |
| --- | ---: | ---: | ---: |
| `Translation/LGraphModel` | 0 | 14.88 s | 633,412 kB |
| `Compiler/ResidualSemantics` | 0 | 3.50 s | 495,256 kB |
| `Translation/OpBridge` | 0 | 47.42 s | 6,809,644 kB |
| the three `*Defs` | 0 | 2.20–4.91 s | 482,276–489,780 kB |
| the three wrappers | 0 | 32.74–35.09 s | 6,727,408–6,773,052 kB |

**Interface audit: IDENTICAL to live.** All 45 `#check` shapes and axiom lists
unchanged by the guards.

**Boundary matrix: `BOUNDARY-MATRIX OK`**, 1.70 s, 473,172 kB — D3's own
`d3_shift_boundary_test.lean`, **32** `#guard`s, run unchanged. Stimulus bounds were
not shrunk.

### 12.4 Reify, ReifyGen and D3Harness onto the clock-aware runtime

D3's three modules carry **zero** clock-aware mentions and call the
**two-argument** `denoteResidual R i s`; master's takes an explicit
`ClockEdges`. The migration surface is small and was checked rather than
assumed: `ReifyGen` emits no reference to it, `Reify` mentions it only in prose,
and the real call sites are **two**, both in `D3Harness` (`agree` and
`checkerSelfTest`), both with `D` in scope.

Both now pass **`allEdges D` explicitly** — the recorded legacy contract,
written at the call rather than defaulted somewhere, so the contract the
harness checks under is visible.

`Reify` then failed on `flopNext env s idx f`: master's `flopNext` takes a
`ClockEdges`, and its value-level helper `flopNextV` **does not exist on master
at all** — it is a D3 addition, and D3's is clockless (no edge, no
`asyncReset`, reset arm `if inReset then …`). So it is carried, not copied:
`flopNextV` gains `edge` and `asyncReset` and reproduces `flopNext`'s rule
exactly — a quiet domain holds, except for an asynchronous reset, which does
not wait for an edge — and `flopNext_eq` is restated with `fires e f.clock` and
`f.asyncReset` passed explicitly. A reifier that silently assumed a firing edge
would be stating something false of a quiet domain.

Inserting that helper reproduced the **same** defect as the splitter's: placed
before `def flopNext`, it landed between that declaration and its doc comment
and left the comment dangling. Re-inserted above the comment.

### 12.5 Full chain rebuilt with the reifier included

Twelve modules, all exit 0, rebuilt against the guarded `ResidualSemantics`
with the migrated reifier and harness:

| module | exit | wall | peak RSS |
| --- | ---: | ---: | ---: |
| `Translation/LGraphModel` | 0 | 12.47 s | 638,308 kB |
| `Compiler/ResidualSemantics` | 0 | 1.21 s | 494,364 kB |
| `Translation/OpBridge` | 0 | 28.88 s | 6,819,368 kB |
| `Compiler/CompileOpDefs` | 0 | 0.93 s | 489,048 kB |
| `Compiler/CompileGraphDefs` | 0 | 1.10 s | 482,860 kB |
| `Compiler/CompileDesignDefs` | 0 | 0.92 s | 485,648 kB |
| `Compiler/CompileOp` | 0 | 14.20 s | 6,774,064 kB |
| `Compiler/CompileGraph` | 0 | 15.06 s | 6,726,720 kB |
| `Compiler/CompileDesign` | 0 | 14.04 s | 6,750,788 kB |
| `Compiler/Reify` | 0 | 1.03 s | 512,276 kB |
| `Compiler/ReifyGen` | 0 | 8.80 s | 1,600,816 kB |
| `Compiler/D3Harness` | 0 | 1.23 s | 497,532 kB |

**Interface audit: IDENTICAL to live** again, with the reifier in the tree.
**Boundary matrix: `BOUNDARY-MATRIX OK`**, 1.01 s, 469,972 kB.

The reifier path sits where the split intends it: `Reify` 512 MB, `D3Harness`
498 MB, `ReifyGen` 1.60 GB — against 6.7–6.8 GB for anything that imports
Mathlib.

**Live library after every isolated build: 25 files checked, 0 changed.**

### 12.6 Still open on D3

- `ReifyProof.lean` is ported and builds (§8 corrects the false hole count
  that had deferred it). Generated-proof acceptance is a separate matter, and
  each emitted theorem is audited individually.
- The sweep/manifest scripts still carry D3's private absolute paths.
- `agree` remains **sampled** evidence and `proof` remains **unattempted** —
  neither is upgraded by anything here.

## 13. ReifyGen emitted clockless models — building it proved nothing

`ReifyGen` built, exit 0, and that checked **the generator**. The terms a
generator writes are elaborated only when a design is actually reified, so
nothing about them had been checked at all.

Run on a real certificate, three of the four commands failed outright:
`reify_design_named`, `reify_design_shared` and `reify_design_chunked` each
emitted a `flopNextV` application of type `Bool → Bool → BV` where `BV` was
required — the two new arguments were simply missing at `ReifyGen.lean` lines
294, 395 and 580. The file was byte-identical to pinned D3; adding the
parameters to `flopNextV` had not touched its callers in the emitter.

And the fourth was worse for being silent: `reify_design` hand-rolls its flop
update and has **no notion of an edge at all** — it applies the reset whenever
the pin is asserted, then takes `din` or holds on the **enable alone**. It
compiles. It is simply wrong on any step where the domain is quiet.

### 13.1 What was repaired

| path | flop update | memory |
| --- | --- | --- |
| `reify_design` (default) | hand-rolled, clockless → now emits `flopNextV` | committed unconditionally → gated |
| `reify_design_named` | missing 2 args | committed unconditionally → gated |
| `reify_design_shared` | missing 2 args | committed unconditionally → gated |
| `reify_design_chunked` | missing 2 args | committed unconditionally → gated |

**The memory arm was wrong in all four.** `denoteResidual` commits a memory
only when `fires e m.clock` and otherwise **holds** the previous image; every
generated model committed on every step. A quiet domain's memory was being
written.

The default path's hand-rolled update could not be repaired in place —
`flopNext`'s rule is three-way (a quiet domain holds, *except* for an
asynchronous reset, which does not wait for an edge) and the hand-rolled shape
has nowhere to put the edge. It now emits the same `flopNextV` application as
the other three: one rule, one place, already bridged by `flopNext_eq`.

All four generated models take an explicit `(e : ClockEdges)` as their first
parameter. D3's historical comparison is preserved as the **explicitly named**
`allEdges D` at the two `D3Harness` call sites — a wrapper around an
edge-aware model, not a default hidden inside one.

### 13.2 The regression that catches it

`probes/d3_generated_clock_test.lean` runs **all four commands** on one
certificate and compares each generated model against `denoteResidual` **on the
same edge vector, input and state**. A build-only or shape-only check cannot
catch any of the above.

The fixture carries, deliberately: two clock domains; a **synchronous**
active-high reset and an **asynchronous** active-low one; a **nonzero** reset
value (7); an **enable**, so the hold path is reached with the clock firing; a
memory on the second domain; and distinct source and output widths (8-bit
sources, a 4-bit output).

**The memory claim in the first version of this fixture was not substantiated.**
Its `nextImg` pointed straight at the memory's own `memImg` **source**, so the
commit arm and the hold arm produced the *same function* — the comparison could
not have told them apart, and the single whole-result non-vacuity guard was
being satisfied by the flops alone. Corrected: an `Op_MemWrite`
`[mem, addr, data, enable]` node now stands between them, and non-vacuity is
stated **per observable** rather than globally:

- the flops must differ between quiet and simultaneous edges;
- the **memory** must differ at the written cell between a held and a committed
  step;
- the written cell takes the written value (200) when its domain fires and
  holds the previous image (5) when it does not;
- a **neighbour** cell is 5 either way;
- with the write enable low, even a firing domain changes nothing.

Four edge vectors — **quiet** `#[f,f]`, **independent** each way `#[t,f]` and
`#[f,t]`, and **simultaneous** `#[t,t]` — crossed with five input cases
covering reset asserted and released on each polarity and the enable high and
low: **20 comparisons per command, 80 in all**, each on outputs, flop state and
the memory image read at four addresses.

Plus a **non-vacuity** guard: the quiet and simultaneous edge vectors must give
*different* results on the same input, or the comparison would pass for a model
that ignored its edges entirely.

Result: `GENERATED-CLOCK-REGRESSION OK`, exit 0.

### 13.3 Two further defects found by running it

Fixing the emission exposed a second layer, both hygiene:

- a bare `Compiler.mk_bv` inside a syntax quotation is hygienically marked and
  does not resolve where the generated code is elaborated —
  `Unknown identifier Compiler.mk_bv✝`. Spliced as `$(mkIdent ...)` instead;
- and it is not `Compiler.mk_bv` at all: `mk_bv` is at the **root**. The first
  correction swapped one unknown name for another, and only running the
  fixture again showed it.

### 13.4 Full chain, re-verified with the repaired emitter

Twelve modules, all exit 0, rebuilt after the emitter repair:
`LGraphModel` 11.76 s / 659,056 kB · `ResidualSemantics` 1.49 s / 497,892 kB ·
`OpBridge` 1:10.51 / 6,828,764 kB · the three `*Defs` 1.29–1.99 s /
486,680–487,816 kB · the three wrappers 13.15–25.41 s / 6,726,456–6,779,632 kB ·
`Reify` 1.01 s / 515,716 kB · `ReifyGen` 7.61 s / 1,619,564 kB ·
`D3Harness` 1.10 s / 497,036 kB.

| check | result |
| --- | --- |
| 45-theorem interface audit | **IDENTICAL to live** |
| generated-model clock regression | **`GENERATED-CLOCK-REGRESSION OK`**, 3.19 s, 1,511,380 kB |
| boundary matrix (32 `#guard`s) | **`BOUNDARY-MATRIX OK`**, 1.07 s, 472,552 kB |
| live library after every isolated build | **25 checked, 0 changed** |

The regression is now part of the staged build script, so the emitted terms are
checked every time the generator is, rather than only when someone thinks to
reify a design.

## 14. The historical D3 fixtures, and why two `denoteResidual` calls were not the migration

§12.4 claimed the reifier migration on the strength of two `denoteResidual`
call sites in `D3Harness`. That was not the migration. **`D3Harness.agree` and
`distinctObservables` still take a two-argument `fast`**, while the generated
models now take three — so every historical fixture that passes a generated
model to them fails to elaborate.

### 14.1 A gate marker is not acceptance

Run as pinned, `d3_shared_test.lean` **prints `D3SHARED OK` and exits 1** with
68 errors. So does `d3_flopnext_test` (`D3FLOPNEXT OK`, 59 errors) and
`d3_stimulus_test` (`D3STIM OK`, 15 errors). The marker is printed by an
`#eval` that runs regardless of whether the `#guard`s above it elaborated.

The probe runner therefore requires **exit 0 AND zero error lines**, and
reports which it was. Reading for the marker alone would have accepted three
broken fixtures.

### 14.2 Caller-only adaptations

Every change is at a **call site**; no stimulus, assertion or expected value
was altered.

| fixture | adaptation |
| --- | --- |
| `d3_shared_test` | `eqOn` takes edge-aware models and applies `allEdges D` once, so all 20 `#guard`s stay byte-identical; 8 `D3.agree` calls wrap their **model argument** `(fun i s => m (allEdges D) i s)`, leaving `agree`'s own two-argument `fast` untouched |
| `d3_flopnext_test` | 18 `flopNextV` applications gain `true false` — D3's clockless helper always committed, which is this helper with the domain **firing** and the reset **synchronous**, so each original case keeps its original meaning; its `ResidualFlopUpdate` literal gains `clock := 0, asyncReset := false`; 6 `flopNext` calls gain an explicit `eF : ClockEdges := #[true]` |
| `d3_stimulus_test` | 4 `compileAndRun` / `distinctObservables` callers gain `allEdges zeroDetectCert` |

The quiet and asynchronous arms those fixtures cannot reach are exercised by
`d3_generated_clock_test` instead (§13.2).

### 14.3 Results

| probe | result |
| --- | --- |
| `d3_flopnext_test` | ok — exit 0, 0 errors, `D3FLOPNEXT OK` |
| `d3_generated_clock_test` | ok — `GENERATED-CLOCK-REGRESSION OK` |
| `d3_harness_test` | ok — unchanged from pinned, `D3HARNESS-TEST OK` |
| `d3_memory_semantics_test` | ok — unchanged from pinned, `D3MEMSEM OK` |
| `d3_shared_test` | ok — exit 0, 0 errors, `D3SHARED OK` |
| `d3_shift_boundary_test` | ok — `BOUNDARY-MATRIX OK` |
| `d3_stimulus_test` | ok — exit 0, 0 errors, `D3STIM OK` |
| `d3_flop_gen_test` | superseded — see §15; `ReifyProof` is ported |
| `d3_memory_gen_test` | superseded — see §15; `ReifyProof` is ported |

**That classification was wrong**, and rested on a miscount: `ReifyProof` has
no holes and builds unmodified (§8). Both fixtures are required, and §15 records
their actual state.

`agree` remains **sampled** evidence and `proof` remains **unattempted** —
nothing here changes either.

## 15. The proof emitters, and the metadata they were dropping

With `ReifyProof` ported (§8), all three emitters needed the same two repairs.

### 15.1 Edge quantification

`prove_reified`, `prove_reified_chunked` and `prove_reified_incr` each emitted

```
∀ i st, F i st = compileAndRun D i st        and  … = interpretDesign D i st
```

which no longer typechecks. All three now emit `∀ e i st, F e i st = …`,
**generically** — not specialised to `allEdges D`, so a per-design proof still
says what it said before about every schedule rather than only the all-firing
one.

### 15.2 The generated facts were dropping clock metadata

The real loss was elsewhere. Both the chunked and the incremental builders
reconstruct `D_flops` and `D_mems` as record **literals**:

```lean
`({ width := …, din := …, enable := …, resetPin := …,
    resetValue := …, resetActiveLow := … })          -- no clock, no asyncReset
`({ aw := …, dw := …, nextImg := … })                -- no clock
```

`FlopDesc.clock` defaults to `0` and `asyncReset` to `false`, and
`MemoryDesc.clock` to `0`. So a literal that omits them still **elaborates**,
and the `theorem … : D.flops = <literal> := rfl` still **closes** — for any
design whose flops happen to sit on ordinal 0 with a synchronous reset. On
clock ordinal 1, or an async reset, the two records differ, the `rfl` fails,
and the generated proof falls onto `sorryAx` — which `d3_proof_gate` then
correctly rejects.

**Four literals, in two builders, all repaired** by carrying `fu.clock`,
`fu.asyncReset` and `mu.clock` from the residual updates, which already hold
them. Nothing is defaulted to force the `rfl` and nothing is specialised to
`allEdges`. `ReifyGen`, `Reify` and `D3Harness` were audited for the same
omission and have no descriptor literals at all.

### 15.3 Why the original fixtures could not catch it

Every pinned D3 fixture sits on clock ordinal 0 with a synchronous reset —
exactly the defaults. Caller adaptation alone would have left the loss in
place and passing.

`probes/d3_nondefault_metadata_test.lean` is the regression: a flop on **clock
ordinal 1** with an **asynchronous, active-low** reset and a nonzero reset
value; a second flop on ordinal 0, synchronous and active-high, so the domains
are genuinely independent; a **writable memory on ordinal 1**; distinct source
and output widths. It runs an emitter, puts the generated theorem through
`d3_proof_gate`, compares the model against `denoteResidual` on quiet,
independent and simultaneous edges, and pins the arms a defaulted
`asyncReset := false` would lose — the async reset acting on a **quiet**
domain, the synchronous flop holding on its own quiet domain, and the memory
committing on its own domain only.

### 15.4 Results

All eleven probes: **exit 0, zero errors, 0 failed.**

| probe | marker | peak RSS |
| --- | --- | ---: |
| `d3_flop_gen_test` | `D3FLOPGEN OK` | 6,843,936 kB |
| `d3_flopnext_test` | `D3FLOPNEXT OK` | 471,456 kB |
| `d3_generated_clock_test` | `GENERATED-CLOCK-REGRESSION OK` | 1,512,036 kB |
| `d3_harness_test` | `D3HARNESS-TEST OK` | 480,680 kB |
| `d3_memory_gen_test` | `D3MEMGEN OK` | 6,841,408 kB |
| `d3_memory_semantics_test` | `D3MEMSEM OK` | 469,956 kB |
| `d3_nondefault_metadata_test` | `D3-NONDEFAULT-METADATA OK` | 6,837,416 kB |
| `d3_reify_test` | (no marker) | 6,840,032 kB |
| `d3_shared_test` | `D3SHARED OK` | 1,514,424 kB |
| `d3_shift_boundary_test` | `BOUNDARY-MATRIX OK` | 469,636 kB |
| `d3_stimulus_test` | `D3STIM OK` | 476,024 kB |

Two fixture mistakes of mine along the way, both recorded rather than papered
over: the `eAll_<cert>` compatibility bindings were first inserted **after**
their first use, and my new regression asserted the *other* flop's initial
value (`3` where the stored value is `9`). Neither was a semantic failure, and
neither implementation nor any original assertion was changed to accommodate
them.

## 16. Installed into the tracked tree

The D3 contribution is no longer staged-only. 27 files are installed in
the owned master paths, with the post-D2 schema and master's architecture
retained throughout.

| destination | status | sha256 | provenance |
| --- | --- | --- | --- |
| `formal/lean/LeanSemanticPrimitives/Compiler/CompileOpDefs.lean` | new | `7d0a93539e3ea51c` | adapted (see §12–§15) |
| `formal/lean/LeanSemanticPrimitives/Compiler/CompileGraphDefs.lean` | new | `1419933c3b1bc9b5` | adapted (see §12–§15) |
| `formal/lean/LeanSemanticPrimitives/Compiler/CompileDesignDefs.lean` | new | `60043dac56f5c3bf` | adapted (see §12–§15) |
| `formal/lean/LeanSemanticPrimitives/Compiler/CompileOp.lean` | replaced | `3e82e14d2dc7cbf6` | re-derived / new |
| `formal/lean/LeanSemanticPrimitives/Compiler/CompileGraph.lean` | replaced | `cf582b4d77beb451` | re-derived / new |
| `formal/lean/LeanSemanticPrimitives/Compiler/CompileDesign.lean` | replaced | `73d0ba3551c86c7f` | re-derived / new |
| `formal/lean/LeanSemanticPrimitives/Compiler/ResidualSemantics.lean` | replaced | `7486cf3f076668a2` | re-derived / new |
| `formal/lean/LeanSemanticPrimitives/Compiler/Reify.lean` | new | `32a29df183079160` | re-derived / new |
| `formal/lean/LeanSemanticPrimitives/Compiler/ReifyGen.lean` | new | `f248fd8c4cf8b703` | adapted (see §12–§15) |
| `formal/lean/LeanSemanticPrimitives/Compiler/ReifyProof.lean` | new | `fdb598f754483904` | adapted (see §12–§15) |
| `formal/lean/LeanSemanticPrimitives/Compiler/D3Harness.lean` | new | `81418798bc7cbb78` | adapted (see §12–§15) |
| `formal/lean/LeanSemanticPrimitives/Translation/LGraphModel.lean` | replaced | `f18d2c8888668171` | re-derived / new |
| `formal/lean/LeanSemanticPrimitives/Translation/OpBridge.lean` | replaced | `fc9366004cb12f1d` | re-derived / new |
| `formal/lean/probes/d3_flop_gen_test.lean` | new | `5ad35e10dc8f5bbb` | re-derived / new |
| `formal/lean/probes/d3_flopnext_test.lean` | new | `11af408ef81c0b8b` | re-derived / new |
| `formal/lean/probes/d3_generated_clock_test.lean` | new | `eb199e97bc58a51a` | re-derived / new |
| `formal/lean/probes/d3_harness_test.lean` | new | `c384ea0374b9d496` | re-derived / new |
| `formal/lean/probes/d3_memory_gen_test.lean` | new | `fae4ea6b665832f3` | re-derived / new |
| `formal/lean/probes/d3_memory_semantics_test.lean` | new | `eb9d6a3ac1d829b2` | re-derived / new |
| `formal/lean/probes/d3_nondefault_metadata_test.lean` | new | `6462af8bf8ca4743` | re-derived / new |
| `formal/lean/probes/d3_reify_test.lean` | new | `778d5e149a7334b7` | re-derived / new |
| `formal/lean/probes/d3_shared_test.lean` | new | `b27a7b8ed6b14123` | re-derived / new |
| `formal/lean/probes/d3_shift_boundary_test.lean` | new | `66ea49164ef0ff99` | **byte-identical to the D3 pin** |
| `formal/lean/probes/d3_stimulus_test.lean` | new | `609026eafbd04595` | re-derived / new |
| `pass/lean/scripts/d3_sweep.py` | new | `e5cc27ae4383d955` | pinned 979fecc89a1c8dbe, private paths made configurable |
| `pass/lean/scripts/d3_manifest.py` | new | `15b7cb5bc9a4a0f7` | pinned 22327f964c0ebb27, private paths made configurable |
| `pass/lean/manifests/cva6_30_frozen.tsv` | new | `a30f0be7206fb35b` | pinned 70efe8d6c801f798, private paths made configurable |

### 16.1 What is NOT installed

`Compiler/CertIO.lean` stays staged. Its DCERT2 codec, checked admission and
runtime validation all pass their probe, but the **universal round-trip
theorem is not proved yet** and the C++ parity is not measured. Installing a
codec whose central promise is unproved would put it in the tree on the
strength of tests alone.

### 16.2 The live artifacts are the BASELINE environment, not acceptance

The replay queue is still running against `formal/lean/.lake/build`. Those
oleans were built from the **previous** sources and are unchanged — verified
again after the install: **25 recorded files, 0 changed**. `lake env` only sets
environment variables; it does not build, so installing sources cannot disturb
a job in flight.

So the sources in the tree and the artifacts the queue is using are now
deliberately **out of step**, and that is recorded rather than glossed:

- the queue's results are the **baseline compiled environment**, and are not
  evidence about the newly tracked sources;
- the 185 baseline refinement proofs certify the old build, not this one;
- rebuild and activation happen only once the queue is quiescent, under their
  **own** fingerprints, and final integrated benchmarks are taken there.

### 16.3 Evidence corrections carried into this record

- The unmodified pinned `ReifyProof` build was **1:39.65 / 7,135,380 kB**;
  the 1:08.69 / 7,127,316 kB figure I quoted belongs to the **later**
  edge-arity port. Timings are tied to the source they built.
- `csr_regfile`'s full log carries **seven** diagnostics — three constructor
  errors and four `dependsOnNoncomputable` — where the record's
  `lean_errors: 3` is the driver's restricted parser count. Verdict, cause,
  wall and RSS were right; the count was partial.

## 17. The runner emitted clockless callers, and the module backend was missing

§16 called the D3 contribution installed. Two things were wrong with that.

### 17.1 `d3_sweep.py` emitted callers at the old arity

The runner was installed with only its private paths adapted, and its header
said so. But the probes it **emits** called the generated model as a
two-argument function, while it now takes an explicit `ClockEdges`. Every
reifier failed at `D3.report`.

**Ten sites** now pass `Compiler.allEdges <cert>` explicitly — the historical
procedure, in which every clock fires on every step, written out at the call:
the `D3.report`, `D3.agree` and `distinctObservables` callbacks, and the direct
`fast1` / `fast2` / phase-split applications.

Nothing else moved. `D3Harness` keeps its two-argument `fast` contract, the
generated functions keep their edge arguments, and the generated correctness
theorems stay **universally quantified** over `ClockEdges` rather than
specialised to `allEdges`. Samples, observables, mutation controls, the
scheduler and the provenance contracts are untouched. The header's claim that
paths were the only adaptation is corrected in place.

Rendered from the installed runner's own `make_probe` and typechecked against
the repaired prefix:

| reifier | exit | errors | report |
| --- | ---: | ---: | --- |
| legacy | 0 | 0 | `sim_exec=1 obs=255`, `mut_out=rejected`, `agree=1 samples=4 distinct_obs=4` |
| named | 0 | 0 | `agree=1 samples=4 distinct_obs=4` |
| shared | 0 | 0 | `agree=1 samples=4 distinct_obs=4` |
| chunked | 0 | 0 | `agree=1 samples=4 distinct_obs=4` |

### 17.2 The module backend's dependency closure was absent

`--proof-backend module` invokes `d3_module_proof.py`, which was not in master,
nor were its dependencies `d3_gen_modules.py` and `d3_guarded_build.py`. All
three are now installed at their pinned hashes (`4862fa9aeb8ce9a5`,
`eda1ff3531da2d9d`, `8446d0fb07246efc`), along with the manifests that carry
the configured obligations — `coreet_122.tsv`, `cva6_30.tsv`,
`coreet_stages_80.lock.tsv`, `cva6_corpus.tsv`, `cva6_no_certificate.tsv`,
`PROVENANCE.tsv` — and the runner's companions (`d3_gen`, `d3_join`,
`d3_merge`, `d3_cgroup_test`, `d3_manifest_test`, `d3_merge_test`, `d3_bench`,
`d3_bench2`). No private path survives in any of them.

### 17.3 A build-isolation hazard, found before it was run

`d3_module_proof.py` derived its Lean root from its own tracked location:

```python
LEAN = ROOT / "formal/lean"
...
cmd = [..., "lake", "build", f"LeanSemanticPrimitives.Gen.{ns}.{m}"]
run(cmd, ..., cwd=LEAN)
```

It generates `Gen.*` modules **under the live tree** and runs `lake build`
there. With the newly installed D3 sources now newer than the baseline
artifacts, that build would not stop at the generated modules: it would rebuild
`LeanSemanticPrimitives` itself, replacing the oleans a running baseline queue
depends on. Neither `LEAN_PATH` nor the artifact-hash seam prevents it —
`lake build` writes where its workspace is — and `d3_sweep`'s `LAKE` variable
does not reach these calls.

The root is now the seam: `D3_LEAN_ROOT` points the driver at a complete
isolated project, with its own sources and its own `.lake`, and the writes land
there. The default is unchanged, so no existing invocation behaves differently.

**The module backend has NOT been run, and is not credited.** It needs a
complete isolated project root — not merely an olean prefix — and building one
while the baseline queue is draining would contend for the same machine. It is
deferred until the queue is quiescent, with the guards, cgroup enforcement,
imported final-theorem axiom gate and stale-module checks all preserved. A
deferred stage is uncredited, not assumed.

### 17.4 Still not installed from the D3 branch

Recorded rather than glossed as "the D3 contribution is installed":

| file | disposition |
| --- | --- |
| `pass/lean/design_cert_export.hpp` | superseded by master's `certificate_ir.{hpp,cpp}` refactor |
| `pass/lean/d3_scaling*.tsv` (3) | D3's own measurement outputs, not inputs |
| D3's `*.md` records | its branch narrative; this ledger carries the ported content |

### 17.5 Obligations still open

The original **122 CORE-ET** and **frozen 30 CVA6** D3 procedure and
configuration obligations stand, and are not discharged by the control probes
above. So does the full CLI validation: positive and changed-model/malformed
controls for all four reifiers, the phase-only and phase-split paths, `--prove`
on **both** the in-process and module backends, with retained logs, exact final
theorem names and axioms, source and library fingerprints, and wall/RSS.

## 18. Binding the isolation root everywhere, not just in the driver

§17.3 added `D3_LEAN_ROOT` to `d3_module_proof.py`. That was only part of it.
With a non-default root, measured:

- the driver's own `LEAN` moved — but
- `d3_gen_modules.GEN_ROOT` still wrote into the **live**
  `formal/lean/LeanSemanticPrimitives/Gen`;
- the imported sweep's `LEAN_DIR` — and therefore `build_root()`,
  `artifact_dir()`, `artifact_digest()`, `tool_digest()`, the Lake `cwd` and
  the toolchain read — still pointed at the **live** `.lake`;
- so a changed artifact under the isolated root did **not** move
  `olean_snapshot`, and a build would still have written into the baseline
  queue's workspace.

A seam that binds some of the paths is not isolation. All three scripts now
derive every root-dependent path from the one variable — generation, source and
provenance hashes, loaded compiler artifacts, generated-module caches, the Lake
workspace and the composition.

**Verified with two distinct roots.** Stated precisely, because the first
version of this paragraph said "two distinct complete roots" and they were
nothing of the kind: `isoA` held only a `.lake`, `isoB` was empty, and neither
had a lakefile, a `lean-toolchain` or any Lean source. These are
**placeholder-directory path-binding and digest-sensitivity controls**, not
project builds. Every path moves and none leaks to the live tree:

| path | isoA | isoB |
| --- | --- | --- |
| `sweep.LEAN_DIR` | moved | moved |
| `sweep.build_root()` | moved | moved |
| `sweep.artifact_dir()` | moved | moved |
| `gen.GEN_ROOT` | moved | moved |
| `driver.LEAN` | moved | moved |

**Digest sensitivity**, under the isolated root: changing one controlled
`Runtime.olean` moved `artifact_digest()` from `3cc2752f82ef6521` to
`0563344f9f4f8ec0`. That is the digest responding to a change — it is **not**
CLI drift rejection, which is a run refusing on that basis and remains
untested. **Default behaviour is unchanged** — with no variable set, `LEAN_DIR`
is still `formal/lean`.

The stale-module, cgroup and axiom-gate contracts are untouched.

### 18.1 How the restored scripts should be described

Not "byte-identical to the pin". `d3_module_proof.py`, `d3_sweep.py` and
`d3_gen_modules.py` are **from the pinned source with listed adaptations**:

| file | pinned | adaptations |
| --- | --- | --- |
| `d3_sweep.py` | `979fecc89a1c8dbe` | private paths made configurable; ten emitted callers given explicit `allEdges`; `LEAN_DIR` bound to `D3_LEAN_ROOT` |
| `d3_module_proof.py` | `4862fa9aeb8ce9a5` | `LEAN` bound to `D3_LEAN_ROOT` |
| `d3_gen_modules.py` | `eda1ff3531da2d9d` | `GEN_ROOT` bound to `D3_LEAN_ROOT` |
| `d3_guarded_build.py` | `8446d0fb07246efc` | none — byte-identical |
| `d3_manifest.py` | `22327f964c0ebb27` | private paths made configurable |
| `cva6_30_frozen.tsv` | `70efe8d6c801f798` | private paths made configurable |

### 18.2 Still uncredited

The module backend has **not been run**. The root binding is now complete, but
exercising it needs a complete isolated *project* — sources, lakefile and its
own `.lake` — and standing one up while the baseline queue drains would contend
for the same machine. Deferred, and uncredited until it runs correctly. That
does not block the D4 and FP work, which is next.

### 18.3 The source fingerprints were still unbound

§18 claimed "every root-dependent path". It was not: `TOOL_FILES` listed all
**11** Lean sources as `ROOT / "formal/lean/..."`, and `tool_digest` reads those
paths directly. So with `D3_LEAN_ROOT` set, changing an isolated
`D3Harness.lean` left **both** `sweep.tool_digest()` and
`driver.runner_digest()` unchanged — the hash list still named the live
sources.

Bound to `LEAN_DIR`. The runner's own file stays `__file__`: that is the
harness's identity and does not move with the Lean root.

Measured after the fix, with a populated isolated root:

| | before | after | |
| --- | --- | --- | --- |
| `tool_digest()` | `f507457254cb09cb` | `2da3582ebaf6bab4` | detected |
| `runner_digest()` | `6f823474d34c1c4f` | `d60f03edd6aa50c0` | detected |

`TOOL_FILES` names the isolated copy and **not** the live one; with no variable
set it names the live sources again, with `d3_sweep.py` itself as entry 0.
Baseline library: 25 checked, 0 changed.

### 18.4 What is still open on this

The full CLI source-drift admission — a run **refusing** on a changed digest,
rather than the digest merely changing — and the imported module-backend tests.
Both remain untested, and the backend remains deferred and uncredited while the
baseline queue is live.

## 19. D4: the DCERT2 codec, and a redesign I should not have made

### 19.1 The UTF-8 inverse was provable all along

I replaced the length-prefixed UTF-8 clock name with a **code-point** encoding,
on the view that the round-trip needed `String.fromUTF8? s.toUTF8 = some s` and
that the pinned toolchain had no such lemma.

The second half was true; the conclusion was wrong. `String.fromUTF8?` is a
dependent `if` over `IsValidUTF8`, and a `String` carries its own
`isValidUTF8`, so:

```lean
theorem clockName_utf8_inverse (s : String) :
    String.fromUTF8? s.toUTF8 = some s := by
  unfold String.fromUTF8?
  simp only [String.toUTF8_eq_toByteArray, dif_pos s.isValidUTF8]
  congr 1
```

Verified here: exit 0, axioms `[propext, Classical.choice, Quot.sound]`.

**Searching for a named lemma is not the same as trying to prove it.** I ran
`exact?` and grepped the toolchain and Mathlib for `fromUTF8` theorems, found
none, and let that stand in for a proof attempt — then redesigned a wire format
around the gap. The redesign is reverted; the readable encoding stays.

### 19.2 Where the codec stands

`parseCert`/`renderCert` carry the full post-D2 record: the clocks section,
`FlopDesc.clock`, `FlopDesc.asyncReset` and `MemoryDesc.clock`, with the name
length-prefixed and its separator **checked** rather than assumed.

The probe passes — exit 0, zero errors — over: names that are empty, contain a
space, contain a newline, and are multi-byte UTF-8 (`clk_日本語`, `αβγ`);
several clocks with order preserved; the full-metadata record with the new
fields verified carried; the byte-level format including the `3xabc` separator
defect, a missing separator and a newline separator all refused; DCERT1
refused, truncation, a short name, trailing data and empty input refused; the
codec round-tripping a certificate the **checked** read then refuses; and
runtime admission refusing an empty edge vector, an overlong one, a missing
input and a wrong flop state while raw `runChecked` keeps its compiler-only
contract.

### 19.3 The universal round trip — now proved

`CertIORoundTrip.lean` builds, exit 0, zero errors, against the frozen staged
`CertIO`:

```
Compiler.CertIO.parseCert_renderCert : ∀ (D : Compiler.DesignCert),
  Compiler.CertIO.parseCert (Compiler.CertIO.renderCert D) = Except.ok D

'Compiler.CertIO.readClock_reads'     depends on axioms: [propext, Classical.choice, Quot.sound]
'Compiler.CertIO.parseCert_renderCert' depends on axioms: [propext, Classical.choice, Quot.sound]
```

Unconditional over every `DesignCert` on the clock-aware schema: no ASCII
restriction on clock names, no semantic-WF hypothesis, no clock-count or
ordinal bound, no new axiom, no `native_decide`. The source contains zero
`sorry`, zero `axiom`, zero `native_decide`; the four `decide` uses are kernel
decisions of closed `UInt8` facts (`isWs chSpace` and the like), which is why
the axiom set stays at the three standard ones.

The two pieces that were missing:

- **`readClock_reads`.** The clock name is the one field that is not
  whitespace-delimited — a byte count, one *checked* space, then exactly that
  many raw UTF-8 bytes — so the reader's obligation is a `ByteArray.extract`
  fact rather than a digit-scan fact. The bridge from a known list prefix to
  that slice (`extract_of_known_prefix` → `ofList_byteArray_toList` →
  `extract_utf8_from_known_prefix`) plus `clockName_utf8_inverse`
  (`String.fromUTF8? s.toUTF8 = some s`, three tactics) closes it.
  The reduction itself is factored into **`readClock_of_facts`**: stated over
  the caller's facts, with an explicit `change` naming the reduced body. My
  earlier attempt tried to reduce in place and failed — `Option.bind_some` is
  `o.bind some = o`, not `(some a).bind f = f a`, and even with
  `Option.bind_eq_bind` the `some`-bind kept the `extract` out of reach of the
  bounds `simp`.
- **The sixth section.** `hstart` now carries `section' clkBytes D.clocks` as
  the memory section's remainder, and the chain applies
  `readSection readClock clkBody` at `j5`, with `j6`/`hk6`/`hj6` feeding
  `skipWs_final` and `parseCert.go`.

Frozen evidence (all paired and fresh — kept separate from the earlier
`d4codec.log` 09:41 / `d4codec.time` 08:36 pair, which is a different run):

| file | sha256 |
|---|---|
| `src/…/Compiler/CertIO.lean` | `994ff48666bf019c499a9670a3339d7690c0f7cef4b0349be35ab44f7edd0445` |
| `build/…/Compiler/CertIO.olean` | `4c73d7746a6bbc42269e91c436aaee4c881b431a276a533768871e2e493d93ee` |
| `src/…/Compiler/CertIORoundTrip.lean` | `dd68516a87a6a6c119f74c38f41577c8630e0d8ca4a16305b52e30d7a98f7227` |
| `build/…/Compiler/CertIORoundTrip.olean` | `6ed3d944cadd7b6c69b371cb5188769f44ebf63af220fdcacd3b9b97d652b1d0` |
| `rt.log` | `7d58b6250f8e0077a01366b64ce44fa3ff819b6d34657181c93aa7de2c3462dc` |
| `rt.time` | `452cd0bbe66cb08f119a3489abc9196082ee87ca33248f3329631dcd4ad5468c` |
| `certio.log` (empty) | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `certio.time` | `6352e49a93808b068671c22d73aca79730cb2eb1750adcdc2805e9f36ca30b5a` |

`CertIORoundTrip`: 0:40.09 wall, 6,964,476 KiB peak.

The repair came from an independent control
(`automate_workspace/master_rebase/audits/d4_full_roundtrip_port_control_20261010/`),
which is a **proof repair, not an installation or an acceptance** of the module
it was derived from; what is credited here is the build above, on this tree.

### 19.4 Inherited documentation corrected to the actual format

Three statements in the staged modules described the predecessor, not this code,
and are now fixed:

- the wire-format block opened with `DCERT1` and listed an eight-number flop
  record, no `clock`/`asyncReset`, a three-number memory record and no clocks
  section. It now states the actual DCERT2 layout, that `clock` is an ordinal
  written unconditionally, and that a clock record is a count, ONE space and
  exactly that many raw UTF-8 bytes — with why (a name may contain a space or a
  newline; not re-encoding is what makes the round trip hold for every name).
- `parseCert.go`'s cursor comment said `past "DCERT1"`. It now names DCERT2 and
  says where the magic's trailing newline goes (consumed as leading whitespace
  by the first `readNat`).
- the module docstring still charged the deserialiser to the trusted base as
  "`partial` and unverified". `parseCert` is total and now verified; what stays
  trusted is the PRODUCER of the bytes. The bullet says so, and says that
  parsing is not admission.

One inherited claim was **false on this tree**, not merely stale: the round-trip
header credited the C++ link to "the differential parity test
`//pass/lean:dcert_parity`". That target does not exist here — `grep` over
`pass/lean/BUILD` and the C++ sources finds no `dcert_parity` and no DCERT
serializer at all; `pass/lean` emits the Lean literal. The header now states
plainly that the translation-validation link is **unsupported** by this file and
that the runtime input path rests on the literal emitter. Building that link is
the next deliverable (§21).

### 19.5 What is still NOT credited

- **C++↔Lean parity.** No owned `CertificateIR` serializer, no byte or field
  parity run, no version/malformed/truncation/trailing controls on the C++ side.
- **The D4/FP `Compiler.CertIO` facade** is unresolved; nothing is installed
  into tracked paths from D4 yet.


## 21. The C++ side: an owned DCERT2 serializer, and actual parity

### 21.1 What was built

`pass/lean/emit_dcert.{hpp,cpp}` (`//pass/lean:lean_dcert`) serialises a
`CertificateIR` as DCERT2 — the format `Compiler.CertIO.parseCert` is proved to
invert. It walks the SAME IR `emit_design_cert` walks, so the Lean literal and
the byte stream describe one certificate, and it judges nothing: clock ordinals
go out as they stand, admission stays `certWfError`/`readCertChecked`.

Three deliberate decisions, each stated in the source:

- **`canonical_decimal`.** The IR carries integers as decimal STRINGS. Lean
  renders through `Nat.repr`: no leading zeros, no `+`, no `-` on zero. `007`
  and `-0` are the same integer and different BYTES, so they are normalised
  here — and anything that is not an optionally-signed digit run is a refusal,
  never a guess.
- **`dcert_opcode` is its own switch**, not a parse of `format_op`'s
  constructor name. It is exhaustive over `Operation`; the five Lean codes the
  LGraph front end never builds (Sub 2, Div 4, SDiv 6, LT 12, GT 13) are named
  in a comment rather than written as dead cases.
- **`dcert_oparg` preserves the inherited normalisation explicitly.** `Op_Sum`
  and `Op_MemWriteBE` carry a `Nat` in the record's `Int` slot and Lean's
  reader takes `Int.toNat`. The round trip survives because the payload is a
  `uint32_t` and cannot be negative — NOT because that slot is canonical in
  general, which is why the header says so.

### 21.2 Production emission

`pass.lean` in `verified_compiler` mode now publishes THREE artifacts in one
transaction — the Lean literal, `<base>.dcert`, and the IO sidecar — through a
new `publish_group`, of which `publish_pair` is the two-element case and keeps
its documented contract unchanged. DCERT2 is emitted in `verified_compiler`
mode only; the legacy branch's certificate is not a `DesignCert`.

**Control: the other two artifacts are byte-unchanged.** The pass was built
from `HEAD`'s `pass_lean.cpp` and from the new one, and all three DINO designs
emitted with each:

| artifact | SingleCycleCPU | PipelinedCPU | PipelinedDualIssueCPU |
|---|---|---|---|
| `_Lgraph.lean` | byte-identical | byte-identical | byte-identical |
| `_io.json` | byte-identical | byte-identical | byte-identical |

The sidecar still carries `inputs[].ordinal` (including the gaps), `width`,
`used_by_model`, `runtime_input_arity` and `clocks`. Nothing in the async
checker, the `single_edge` contract or any native gate was touched:
`tiny_negedge` still refuses with *"flop n_16 is a NEGEDGE flop (`posclk` = 0).
pass.single_edge must normalize it first"*, and publishes **nothing** — no
artifact, no `.tmp`, no `.bak`.

### 21.3 The write-failure defect, and its permanent control

`Rec` emitted each record's terminating newline from its DESTRUCTOR.
`publish_group` stages to streams with exceptions ENABLED, so a write failure
part way through a record threw, the destructor then wrote during unwinding,
and a second throw out of an implicitly `noexcept` destructor is
`std::terminate` — the process died and `publish_group`'s catch, rollback and
scrub never ran. Independently reproduced against a `streambuf` capped at 12
bytes: **exit −6**, `terminate called after throwing an instance of
'std::__ios_failure'`. At a 128-byte cap the same binary exits 0, which is why
the green golden-byte fixture said nothing about it.

Repaired by removing I/O from the destructor: a record is built in a
`std::string` and handed to the stream by one `write` in ordinary control flow,
so a failure propagates from the call site where it can be caught.

`emit_dcert_test.cpp` keeps the control permanently —
`DcertStreamFailure.MidRecordFailurePropagatesInsteadOfTerminating`, over caps
0, 7, 12, 40 and 200. **Non-vacuity measured**: with the destructor put back,
the test binary aborts with the identical `std::__ios_failure` terminate
message; with the repair, it passes. Byte output is unchanged — all seven
production `.dcert` files are identical before and after, so the repair is
confined to the failure path.

`design_scan_test.cpp` additionally runs the five publication-failure shapes
over the production TRIPLE and over **every slot**, not just the first and last
— a middle element is what a hand-unrolled pair gets wrong: blocked backup,
final-rename failure, staging failure (existing and fresh targets), a duplicate
path, success, and a serialiser that throws mid-stream. The pair controls are
untouched and still pass.

None of this claims crash atomicity. `publish_group` is N renames and says so;
`pass_lean.cpp`'s coherence comment is scoped to publishes that succeed or are
handled, and notes that a third artifact widens the non-atomic window rather
than closing it.

### 21.4 Cross-language parity — actual, on real designs

`pass/lean/scripts/dcert_parity.py` elaborates the emitted literal, renders it
with the library's own `renderCert`, and compares to the C++ bytes.

Byte equality here is **stronger** than a hand-written field walk, and that is
a theorem rather than a hope. `Compiler.CertIO.renderCert_injective`
(`CertIOParity.lean`, standard axioms only):

```
@Compiler.CertIO.renderCert_injective : ∀ {A B : Compiler.DesignCert},
  Compiler.CertIO.renderCert A = Compiler.CertIO.renderCert B → A = B
```

So equal bytes mean the C++ emitter and the Lean literal agree in EVERY field,
including ones no comparison would have thought to check.

Result — **7/7 designs, exit 0, every check**:

| design | bytes | parsed |
|---|---:|---|
| `SingleCycleCPU` | 35,710 | 810 src, 1113 nodes, 9 out, 33 flops, 0 mem, 1 clk |
| `PipelinedCPU` | 48,348 | 1007 src, 1533 nodes, 1 clk |
| `PipelinedDualIssueCPU` | 109,395 | 2087 src, 3561 nodes, 1 clk |
| `tiny_ram` | 227 | 1 memory |
| `tiny_sync_ram` | 288 | 1 flop, 1 memory |
| `tiny_byte_ram` | 227 | 1 memory |
| `tiny_async` | 82 | 1 async flop |

Each run checks, on the production bytes: byte parity with `renderCert`; the
verified reader accepts them; re-rendering the parsed design reproduces them;
semantic admission accepts; **sensitivity** to a flop width, a flop clock
ordinal, `asyncReset` and the clock table (a comparison that cannot fail proves
nothing); the codec/admission split on real data — a flop moved to clock
ordinal 999 still PARSES and is REFUSED by `readCertChecked`; and the refusal
controls — DCERT1 magic, truncation, trailing non-whitespace (refused),
trailing whitespace (accepted, as the format intends), opcode 99, a clock
separator that is not a space, a name shorter than its count, and a name
containing both a space and a newline (accepted).

The C++ unit test adds golden bytes for a fixture with every source kind,
arbitrary-precision decimals, non-default flop and memory clocks, an async
reset, and four clock names — empty, plain, one containing a space AND a
newline, one multi-byte UTF-8 — plus an independently written reader compared
field by field, and the decimal/id refusals.

### 21.5 Corrections made here

- `emit_dcert.cpp` and the staged `CertIO.lean` both claimed the first five
  sections stay byte-identical to DCERT1 for a clockless design. **False**: the
  flop record gained `clock` and `asyncReset` and the memory record gained
  `clock`. Both now say the clocks section is last because it is the one this
  version added, and nothing else is inherited.
- `dcert_parity_test.cpp` violated the `_test.cpp` naming rule (no
  `dcert_parity.cpp` exists). Renamed to `emit_dcert_test.cpp`, BUILD updated.
- One control in the parity harness was itself wrong — I wrote a byte count of
  11 for the 7-byte name `a b\nc d`, and the reader correctly refused it. The
  count was fixed; the reader was right.

### 21.6 Fingerprints

The CertIO documentation correction in §19.4 rebuilt the staged modules. The
adopted `CertIORoundTrip` **source and object are byte-identical** to the
audited ones (`dd68516a…`, `6ed3d944…`) — the edit was to `CertIO`'s comments
and does not reach the dependent object. Current set:

| file | sha256 |
|---|---|
| `CertIO.lean` | `a7eefe528a6a337016c73d79d5350767401011433a0cc15165b3f93b7656f918` |
| `CertIO.olean` | `27a0536f5e0858cee2ae6db21d98e7299a81d9747abce33d7a988c2d9b4fa8a4` |
| `CertIORoundTrip.lean` | `dd68516a87a6a6c119f74c38f41577c8630e0d8ca4a16305b52e30d7a98f7227` |
| `CertIORoundTrip.olean` | `6ed3d944cadd7b6c69b371cb5188769f44ebf63af220fdcacd3b9b97d652b1d0` |
| `CertIOParity.lean` | `d4d206cadb4cb02cc5017ebea37fc53cf2fdc454f330f2fd8883ba80cfd4dd94` |
| `CertIOParity.olean` | `7df06df0922c80971bb58f00a2de3b67a085df354bc9416e1d22733599d6d030` |

CertIO 0:13.44 / 6,728,112 KiB; CertIORoundTrip 0:34.20 / 6,956,868 KiB;
CertIOParity 0:18.96 / 6,705,184 KiB. All exit 0, zero errors. The live
baseline library is **unchanged**: no `.olean` under
`formal/lean/.lake/build/.../LeanSemanticPrimitives` has been written today.

### 21.7 Still NOT credited

The D4/FP `Compiler.CertIO` facade is unresolved, so **nothing from D4 is
installed into tracked paths** — the Lean modules remain staged. The original
D4 runtime-loading and fixed-certificate replay procedures are not yet
restored, and a new certificate PRODUCER is not fixed-input replay of
historical rows. The pinned FP contribution, the D3 scaling/CLI/backend
obligations and final integrated validation under final fingerprints all
remain.

## 22. The `Compiler.CertIO` collision, resolved without dropping either side

§11.6 recorded that D4 and FP both define `Compiler.CertIO` and that neither
may be dropped. Both now exist, built and probed.

### 22.1 What the two actually are

They are not two spellings of one thing:

| | FP | D4 |
|---|---|---|
| format | **DCERT1** — 8-number flop record, 3-number memory record, no clocks section | **DCERT2** — adds `clocks`, `FlopDesc.clock`, `asyncReset`, `MemoryDesc.clock` |
| runtime | none | `runChecked`, `runAdmitted`, `runChecked_correct`, `loadAndRun`, `runCycles` |
| admission | `asyncOK`, a **transport** constraint | `certWfError`, a **design** predicate |

FP's `asyncOK` exists *because* DCERT1 cannot carry the commit-side async flag:
a file whose `flopQAsync` sources disagree with the flops it parses to is
describing a different design from the one its exporter meant.

### 22.2 The resolution

- **`Compiler.CertIO.V1`** (`CertIOV1.lean`) holds FP's codec **verbatim**
  apart from the namespace, compiled against master's clock-aware record. 519
  lines, exit 0.
- **`Compiler.CertIO`** keeps the unqualified name and is the DCERT2 codec,
  because DCERT2 is what the C++ pass now emits.
- **`Compiler.CertIOFacade`** imports both, routes on the magic, and calls each
  side's own admission. It defines **no third policy**: `admitV2` *is*
  `readCertChecked`; `admitV1` *is* parse-then-`asyncOK` and deliberately does
  **not** also run `certWfError`, which would impose on a DCERT1 file a check
  FP never ran, about fields the format never carried.

Two theorems make the routing safe rather than merely plausible, both on
standard axioms:

```
magics_are_exclusive : ∀ (b : ByteArray), ¬(hasMagic b ∧ V1.hasMagic b)
parseAny_renderCert  : ∀ (D : DesignCert), parseAny (renderCert D) = .ok (Format.v2, D)
```

`parseAny` returns the FORMAT alongside the design, because a DCERT1 file
parses to a certificate indistinguishable from a genuinely one-clock DCERT2
one — and they do not mean the same thing.

### 22.3 The loss is proved, not asserted

Over the clock-aware record FP's renderer is **not injective** — `clock`,
`asyncReset` and `clocks` have no bytes to go to:

```
V1.flopBody_ignores_clock_metadata : ∀ (f : FlopDesc) (c : Nat) (a : Bool),
  flopBody { f with clock := c, asyncReset := a } = flopBody f
V1.renderCert_not_injective : ∃ A B : DesignCert, A ≠ B ∧ renderCert A = renderCert B
```

(`[propext, Quot.sound]` — not even choice.) So no theorem of the shape
`parseCert (renderCert D) = .ok D` can hold for V1 over every `D`. That is
precisely why the version was bumped rather than extended in place, and why
`Compiler.CertIO.parseCert` refuses a DCERT1 stream by name instead of reading
it as a one-clock design. It is now a proof where it was a design note.

### 22.4 Probed, not just compiled

`facade_probe.lean`, exit 0, `FACADE OK`, eight checks: DCERT2 routes to v2
with the clock table, ordinal and `asyncReset` intact; DCERT1 routes to v1 and
the metadata it cannot carry is **absent** rather than invented; an unknown
magic is refused; `admitV1` still refuses an async design carried over DCERT1
and still accepts a synchronous one; `admitV1` does **not** apply
`certWfError` while `admitV2` still does, on the same out-of-range output; and
the two codecs produce different bytes for one design.

### 22.5 The obligation this does NOT discharge

Any FP call site that `open`s `Compiler.CertIO` and calls
`parseCert`/`renderCert`/`loadCert` must be repointed at
`Compiler.CertIO.V1`, or it silently acquires the DCERT2 contract. That is a
migration obligation on the 38 `Projection` modules and is **not** done.

| file | sha256 |
|---|---|
| `CertIOV1.lean` | `56320c6cb6f7af9cbdcb838a331ed9ca9772cab1adb03cdc0de7896338c8e015` |
| `CertIOV1.olean` | `c74e7de0570f61eacfce652495e4051ab92c78bb566ef48644d23d9a70bf7d1f` |
| `CertIOFacade.lean` | `de9355d71f47ee63ffc1bbadfd2b3180dd211fee2ab28eb187520ecfc604b056` |
| `CertIOFacade.olean` | `4e122ba3aeae26c3fe3a41e181bf33bfa27df4f489e91d724f4d47cd0fdfd428` |
| `facade_probe.lean` | `aa96cca8e2910c91b282b094335b12735fb551362c52272f8d0ed1b30d517910` |
| `facade_probe.log` | `f15d69fc011bb59119d2ca85d9b4f2082d7773722bb6a09977ab8db4c552189f` |

CertIOV1 0:02.30 / 513,708 KiB; CertIOFacade 0:20.72 / 6,722,312 KiB.

## 20. D3 replay backlog (recorded, not yet repaired)

Two gaps in restored D3 procedures, kept visible so they are not mistaken for
done:

- **`d3_gen.py`** — the scaling generator still emits **old-arity**
  `denoteResidual` calls. Its n=2 chain outputs for naive, composed and pruned
  all fail typechecking against the repaired library. The original scaling
  procedures are **not credited** until it is adapted and validated, preserving
  the variants, shapes, sizes and proof obligations.
- **`d3_bench.sh` / `d3_bench2.sh`** — they write timing and log files under
  `/tmp`, against this workspace's project-local rule, and their status logic
  can label a non-timeout non-zero exit `OK` when its narrow error-text pattern
  misses. Neither has been executed here.
