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
| **`ReifyProof.lean`** | 1,327 | **4** |

`ReifyProof.lean`'s four `sorry`s are a **named incompleteness that travels
with the port** and must not be absorbed silently: nothing depending on them
may be reported as PROVEN.

**Why this matters here beyond D3.** The standing D3 sweep budget is ~20 GB
aggregate RSS, and the DINO measurements in this ledger run 8.4–13.3 GB per
job. A definition path that does not drag Mathlib in is the difference between
a probe that fits and one that does not. It is also the right carrier for the
direct sweep (R35), whose probes import `DirectTrace` for exactly this reason.

**Port order for D3:** the `Defs` split first (it is mechanical, `sorry`-free
and independently valuable), `Reify`/`ReifyGen` next, `ReifyProof` last and
with its four holes declared.

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
4. **Declared holes stay declared** — `ReifyProof`'s four `sorry`s above all.
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

## 8. Deferred, and explicitly outside any accepted theorem path

`ReifyProof.lean` carries **4 `sorry`s in 1,327 lines**. They travel with the
port as a declared incompleteness: nothing depending on them may be reported as
PROVEN, and no accepted obligation may route through them.

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

- `ReifyProof.lean` (1,327 lines, **4 `sorry`**) is **not** ported. It stays
  outside every accepted proof path, and nothing above depends on it.
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
| `d3_flop_gen_test` | **BLOCKED** — imports `ReifyProof` |
| `d3_memory_gen_test` | **BLOCKED** — imports `ReifyProof` |

**BLOCKED is not FAILED.** Those two import the one module deliberately not
ported, the one carrying four inherited `sorry`. Calling them failures would
read as a regression in the port; they are blocked on a declared deferral, and
they stay outside every accepted proof path.

`agree` remains **sampled** evidence and `proof` remains **unattempted** —
nothing here changes either.
