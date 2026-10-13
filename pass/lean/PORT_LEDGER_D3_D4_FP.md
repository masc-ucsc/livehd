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

### 6.4 Final acceptance, across all three branches: generated simulator source, byte for byte

Carried into the plan as a **final-acceptance** requirement. It runs once, at
acceptance, and it is a SAMPLE check -- not a gate on any intermediate
checkpoint, and not a substitute for any branch's own acceptance above.

For each original branch (D3, D4, FP), independently:

1. **Freeze 10 supported blocks.** Written down before anything is generated,
   so the sample cannot be re-picked after a mismatch. "Supported" means the
   block is one that branch can emit a certificate for; a block that neither
   side can generate is not a passing comparison, it is an absent one, and it
   does not count toward the ten.
2. **Generate the simulator source twice**: once on that branch, once on the
   corresponding rebased final-master path. `pass/lean/scripts/make_sim_launcher.py`
   is the generator on master (its output is `formal/lean/Sims/`); the branch's
   own generator is whatever that branch pins.
3. **Identical inputs, toolchain and options** on both sides. The same
   certificate bytes in, the same Lean/Lake toolchain, the same generator
   flags. A difference in any of those makes a mismatch uninterpretable.
4. **Require exact raw byte equality** of the generated source. Not
   normalized, not whitespace-insensitive, not "equivalent after formatting".
5. **Retain the evidence**: the sha256 of every input and every generated file
   on both sides, and the full diff of every mismatch. A recorded mismatch is
   the product of this check just as much as a match is.

Three substitutions are explicitly NOT permitted, because each one answers a
different question:

- **behavioral equality** -- running both simulators and comparing outputs. It
  passes whenever two different generators happen to agree on the sampled
  stimulus, which is precisely the gap a byte comparison closes.
- **cross-backend comparison** -- comparing one side's simulator against some
  other execution path. The claim is that the REBASE preserved the generator,
  not that two implementations agree.
- **any new equivalence proof.** None is required here. This is a source
  identity check with a retained artifact, and arriving at it by way of a
  proof obligation would change what is being accepted.

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

## 23. The original D4 procedures, restored

### 23.1 What came back

From the pin `f6ad1fe29f8f15aeda045115864fd81a34c33aef`, **134 files**:

| group | files |
|---|---|
| runtime + replay drivers | `run_d4_runtime_sweep.sh`, `replay_d4_certs.sh`, `verify_d4_run.sh`, `d4_scope_verdict.sh`, `classify_d4_rows.sh`, `d4_emit_in_scope.sh`, `lean_cert_to_dcert.py` |
| probes | `d4_sweep_row`, `d4_loadone`, `d4_write_tiny_cert`, `d4_shl_regression` |
| `pass/lean/tests/d4/` | 114 files — 34 control scripts, 5 Python tools, 37 TSV tables, 26 txt records, 9 module lists, 2 docs, 1 BUILD |

Every file landed at its pinned hash first; the adaptations below are recorded
per file in `generated/master_port/d4_restore_manifest.json`.

Preserved exactly, because they are the procedure: the **18-column row
contract and its order**, the fixed-certificate **SHA binding** with no
re-emission during replay, the ordinary **8192 KiB stack**, resource-limit
decisions **ahead of** accepting or digesting a surviving payload, canonical
terminal cgroup evidence and cleanup, fail-closed unknown evidence, and the
binary/library/config identities. The historical tables are restored as
**historical provenance** — the read-only reconciliation controls consume them
as input; none of their rows is fresh benchmark credit.

### 23.2 Adaptations, each one named

- **DCERT2, not DCERT1.** The probes and the Python reader walk the clock-aware
  format. A historical DCERT1 file is **refused by magic**, never re-read as a
  one-clock design — which would invent a domain the bytes do not state.
- **`runCheckedAllEdges`, not `runChecked`.** These procedures predate clock
  edges and mean every declared clock fires. Naming the contract keeps the
  assumption at the call site instead of defaulting it underneath.
- **`d4_loadone` takes a path.** The pinned version embedded the literal
  `CERTDIR/<name>.pyconv`, a placeholder a caller had to `sed` before every
  use. That is a template, not a runnable procedure, and a stale substitution
  is invisible in the output. The directory now comes from `D4_CERT_DIR` or an
  argument, and **the path actually read is printed on every row**.
- **`dcert_delta.py` walks BYTE OFFSETS.** Its cursor tokenized on whitespace;
  a clock name is taken by count and may contain a space or a newline, so a
  tokenizer cannot see such a file at all. Whitespace is now skipped exactly
  where `Compiler.CertIO.skipWs` skips it. Every refusal it had, it still has,
  plus: a name that is not valid UTF-8, a separator that is not the single
  space, a name shorter than its count, and a clock ordinal outside the table.
- **The delta metrics are clock-sensitive.** A domain enters a signature
  through its NAME, exactly as a slot enters through the signature of what it
  references — so reordering the clock table is invisible and **moving a flop
  to another domain is not**. Leaving the domain out, which the DCERT1 version
  necessarily did, would report zero structural churn for a design whose flops
  changed clock: the one direction that flatters the result.
- **`dcert_mutate.py` gained five DCERT2 mutations** — `clock`, `mem-clock`,
  `async`, `oor-clock`, `clock-name` — and its grammar-breaking edits moved
  from the token stream to located byte edits, for the same reason as above.

### 23.3 Run, not just restored

```
$ lean --run probes/d4_write_tiny_cert.lean tiny.dcert     -> DCERT2, 104 bytes
$ lean --run probes/d4_sweep_row.lean tiny.dcert
  4  2  1  0  PARSED  1  true  RAN  1  12152281778759549968  -
$ D4_CERT_DIR=... lean --run probes/d4_loadone.lean tiny
  RESULT tiny .../tiny.dcert 2 read=1 parse=1 compilesOk=0(true) run=1
         digest=12152281778759549968
```

The two probes agree on the digest. All three typecheck with zero errors.
`dcert_delta.py` reads production certificates and its **canonicality check**
— re-rendering must reproduce the input byte for byte — passes on them, which
is a third independent confirmation that the C++ serializer's output is exactly
canonical.

### 23.4 Multi-domain coverage, which the production corpus could not give

All nine production parity cases to that point were **single-domain**: one
clock, every ordinal zero. Such a run cannot tell "the ordinal was carried"
from "the field defaulted". `design_scan_test.cpp` now writes scanner-produced
DCERT2 beside the Lean oracle for the two multi-domain cases, and a new
`MemoryNonDefaultDomainCertificate` builds the memory fixture with the RAM on
the *other* clock and **requires** a non-zero memory ordinal:

| certificate | clocks | flop ordinals | memory ordinal |
|---|---:|---|---:|
| `two_domain.dcert` | 2 (`clk_a`, `clk_b`) | 0 and **1** | — |
| `mem_nondefault.dcert` | 2 | **1** and 0 | **1** |

Both pass the full parity gate — byte parity with `renderCert`, reader
acceptance, re-render idempotence, admission, and the sensitivity and refusal
controls. These come from the REAL scanner, not a hand-built fixture, which
would have proved something about my typing instead.

## 24. Exit status was not admission

Both restored drivers captured the runtime child's status and then **ignored
it**, admitting a row on SHAPE alone.

*A correction to my own first account.* I wrote that `$?` after a substitution
ending in `| tail -1` is `tail`'s status and therefore always zero. That is
false where `-o pipefail` is set, and both scripts set it on their second line
— the status was available and correct. The defect was narrower and entirely
in the acceptance test, which never consulted it. Removing the pipe is still
worth doing, because it stops the guard depending on `pipefail` remaining set,
but it was not the repair. Independently reproduced against the actual CLIs: a child printing
a correct 11-field `PARSED/true/RAN` row and exiting 17 was published **with
its digest**, and `replay` reported `runtime_complete=YES`. The preflight had
the same hole one step earlier.

Repaired in both: the acceptance test now requires the status, and stdout goes
to a file so the guard does not rest on `pipefail`. A nonzero or timed-out child cannot contribute an accepted runtime
result however well-formed its row. The logs are retained, an 18-column failure
row is emitted with **no accepted digest**, and a timeout is named as a timeout
even when a complete row arrived first. The preflight now checks its exit AND
that it reported `READ_ERROR` for a missing file. The sweep's own distinction
is untouched: `reconciled=YES` is row shape and completeness, and may still
accompany refused or failed modules. No checker, resource verdict or Lean
semantics is involved.

`pass/lean/tests/d4/exit_status_controls.sh` drives both actual CLIs with a
stub `lake`, over eight legs: exit-0 success, a valid row behind exit 17, a
failing preflight, a timeout after output, and the sweep's own two legs.

**Non-vacuity, measured.** With the pinned drivers restored, the same controls
fail **6 of 8** — `runtime_complete=YES` with a digest from a child that exited
17, and a timed-out run recorded as `PRESERVED`. With the guards, 8 of 8 pass.

### 24.1 The small fixed corpus, and what it did and did not establish

Both CLIs were driven end to end on a fixed corpus — `tiny.dcert`, written by
the restored `d4_write_tiny_cert` probe through the verified renderer:

- **`replay_d4_certs.sh`** with a matching certificate and SHA: exit 0,
  `runtime_complete=YES`, digest `PRESERVED`. With the SHA binding violated,
  with the input missing (the preflight's `/nonexistent.dcert` leg), with a
  well-formed row behind a nonzero exit, and with a timeout after output: all
  refused, no digest published.
- **`run_d4_runtime_sweep.sh selftest`**: a clean 18-column row with its
  digest, and a refused row carrying the cause and no digest.
- **`verify_d4_run.sh`**, read-only, on both runs: **refused**, 17 recomputed
  problems each, naming `emit_rc=<absent>, no-lean`.

That last result is the correct one and is worth stating plainly: the
reconciliation recomputes everything and will not accept a run that lacks the
emitter status, the Lean half and the certificate-to-LGraph evidence it
requires — which a SYNTHETIC selftest fixture does not have. So the CLI is
demonstrated runnable and fail-closed, and **a passing reconciliation still
requires a real corpus run**. Full original-corpus coverage remains open; no
row here is evidence about any design.

## 25. Preflight admission, the corpus prerequisites, and the rebuilt library

### 25.1 Two preflight gaps the exit guard did not reach

- **The sweep's preflight discarded its child's status entirely** — it checked
  the substitution's value and nothing else — so a correctly shaped row
  followed by exit 17 blessed a toolchain that cannot run.
- **Both drivers searched the whole row for `READ_ERROR`.** A perfectly
  successful `PARSED / true / RAN` row satisfies that as soon as the words
  appear in its diagnostic PROSE, so the preflight passed without ever
  exercising the failure path it exists to test.

Both now require a clean exit and the parsed FIELDS — `parse_status`
`READ_ERROR`, `runtime_status` `NOT_RUN`, no compile verdict, no digest — in
one shared `preflight_fields_ok`. The sweep keeps its preflight logs instead of
sending them to `/dev/null`.

`exit_status_controls.sh` has **11 legs**; **the pinned drivers fail 9 of 11**,
the guarded ones pass 11 of 11.

**A correction to my own account**, carried into both drivers, the control
header and §24: I wrote that `$?` after a substitution ending in `| tail -1` is
`tail`'s status and therefore always zero. That is **false** where
`-o pipefail` is set, and both scripts set it on their second line — the status
was available and correct. The defect was entirely in the acceptance test,
which never consulted it. Dropping the pipe is still worth doing, because it
stops the guard depending on `pipefail` remaining set, but it was not the
repair.

### 25.2 The corpus prerequisites

Restored from the pin, both absent and both required by the sweep:

- **`scripts/coreet_preflight.sh`** — binds a CORE-ET run to the corpus it
  measures: cleanliness over `hw dv mk` (the paths the filelist flow actually
  reads), the four derived sources regenerated and compared **byte for byte**
  against a second fresh derivation, and `COREET_TECH` recorded explicitly.
- **`scripts/build_cva6_foundation.sh`** — the one manifest the sweep checks
  revision, config, filelist and wrapper set against.

**A third gap only running it found.** The preflight derives into fresh staging
and compares; master's `coreet_patch_srcs.py` **ignores
`COREET_PATCHED_OUTDIR`** and writes to a hardcoded directory. So the staging
derivation produced nothing and the comparison refused — correctly, and for the
wrong reason. The seam is taken from the pin; master's requirement that
`COREET_ROOT` be set EXPLICITLY is kept, because the pinned copy defaulted it
to a private checkout path. One hunk, each half from whichever side was right.

With that, the preflight **passes**:

```
coreet_rev=c72b6a2fe4aaad5a9eef437776864ff71f4b55d7      coreet_tech=generic
coreet_relevant_inputs_clean=yes
coreet_outside_scope_tracked=0   coreet_outside_scope_untracked=1
coreet_derived_mode=regenerated in place; four outputs AND map then
                    authenticated against a second fresh derivation
coreet_derived_aggregate_sha256=ee945429d58898f9e9555dd487f4f83c7d630ef8b7b25073c1d45a07d82cb4f4
```

The one out-of-scope untracked file is `core_et_blocks.txt`, which the
preflight's own documentation names as retained evidence it must neither delete
nor misreport.

### 25.3 The tracked library, built from source

`lake build` over `CertIOFacade`, `DirectCheck` and the default target: **exit
0, zero errors, 8581 jobs, 1:28.28 wall, 6,922,424 KiB peak, 33 objects.** The
`DirectCheck` repair of R46 holds in the full build.

Fingerprint recorded in
`generated/master_port/d2_runtime/library_fingerprint_rebuilt.txt` — toolchain,
`lhd`, head, and a digest and size for each of the 33 objects. It **supersedes**
`library_fingerprint.txt` rather than replacing it: results accepted against
the previous cached objects stay tied to that file.

### 25.4 The binary binding, made explicit

`LHD` is now honoured exactly when set, under the policy this script already
applied to `LAKE` — a named executable is never replaced by a fallback — and
the provenance records `lhd_bytes` and `lhd_build_mode` (`k8-opt` vs
`k8-fastbuild`, read from the resolved path) alongside the digest.
`bazel-bin/lhd/lhd` is whatever the last build left there, and on this tree
that is a fastbuild, while the accepted corpus rows came from the optimized
build.

### 25.5 The first CORE-ET pilot, and what it showed

Three modules, the optimized binary `54eb7f2b38b10512`, ordinary 8192 KiB
stack, 18-column rows:

| module | emit | runtime | cause |
|---|---|---|---|
| `intpipe_alu` | NO_CERT | NOT_RUN | emitter OK, no `.dcert` found |
| `intpipe_inst_bits_stage` | NO_CERT | NOT_RUN | emitter OK, no `.dcert` found |
| `intpipe_csr_msgs` | EMIT_FAILED (rc=3) | NOT_RUN | static gates: `const width 46 cannot hold value 1152886320234758144 (needs 60 bits)` and four more |

The third is a **genuine static-gate refusal** and is recorded as one; it is
not worked around.

The first two are a **provenance result, not a runtime one**: that optimized
binary was built at 03:23, before `emit_dcert.cpp` existed, so it emits the
Lean literal and the sidecar and no DCERT2 at all. The sweep reported exactly
that — emitter OK, no certificate, nothing run — which is the correct
disposition and not a pass. The optimized binary is being rebuilt with the
serializer; **no runtime coverage is claimed from this pilot.**

### 25.6 CVA6 foundation — my survey was wrong

I reported that no clean CVA6 checkout existed and that the frozen-30
procedure was blocked. **That was my error.**
`/WORKROOT/projects/cva6-blockfinding` is clean at
`6cb200105fb9441d170e45786125a737fab98e91` — exactly the revision
`verify_d4_run.sh` expects, and already the default `CVA6_ROOT` in
`build_cva6_foundation.sh`. It was in my `ls cva6*` output and I simply did not
include it in the loop I then checked, so I concluded "all three are dirty"
from a list of three I had written by hand. The three I did check are indeed
dirty, and nobody's RTL experiment needs reverting.

Rechecked: `tracked-dirty 0`, and the
`cv64a6_imafdc_sv39_hpdcache_wb_config_pkg.sv` the target needs is present.

## 26. A real CORE-ET pilot, end to end

### 26.1 The binary had to be rebuilt first

The first pilot produced **no certificates at all**: two modules `NO_CERT` with
the emitter reporting OK. That was a provenance result, not a runtime one — the
optimized `lhd` was built at 03:23, before `emit_dcert.cpp` existed, so it
emitted the Lean literal and the sidecar and no DCERT2. The sweep reported
exactly that and ran nothing, which is the correct disposition.

Rebuilt: `bazel build -c opt //lhd:lhd` → **`9cdba78e6b236e8c`, 203,557,336
bytes, k8-opt**. The provenance now records `lhd_bytes` and `lhd_build_mode`
beside the digest, so a row says which binary produced it.

### 26.2 The run

`EMIT_MAX_RSS_KB=8388608`, `EMIT_TIMEOUT=3600`, `RUN_TIMEOUT=1800`, serial,
ordinary **8192 KiB** stack, 18-column rows:

| module | emit | parse | compiles | runtime | digest |
|---|---|---|---|---|---|
| `intpipe_alu` | EMITTED | PARSED | true | **RAN** | `4681973968394006261` |
| `intpipe_inst_bits_stage` | EMITTED | PARSED | true | **RAN** | `17698986674452852966` |
| `intpipe_csr_msgs` | EMIT_FAILED (rc=3) | NOT_EMITTED | — | NOT_RUN | — |

`intpipe_alu` is 1536 sources and 2133 nodes; `intpipe_inst_bits_stage` 163/163
with 32 flops. The third is a genuine static-gate refusal —
`const width 46 cannot hold value 1152886320234758144 (needs 60 bits)` and four
more — and its row records *"certificate written and NOT run"*: a partial
certificate surviving a failed emission does **not** become coverage.

That is the whole D4 path on real inputs: **RTL → `lhd` → `.dcert` →
`CertIO.loadCert` → `compilesOk` → `runCheckedAllEdges` → digest**, with no
Lean file elaborated for any of it.

### 26.3 Verification, and what it had to be taught

`verify_d4_run.sh` was written against a CVA6 run and applied its bindings
unconditionally. A CORE-ET run failed four checks for having no `cva6_*`
fields — and, worse, **a CVA6 run was the only thing whose corpus binding was
ever verified**. The corpus now comes from the run's own provenance; absent or
unknown is fatal, and a `selftest` run is refused as driver evidence rather
than corpus evidence. CVA6 keeps every check verbatim including the pinned
revision, overridable only through `D4_EXPECT_CVA6_REV` with the override
**printed**. CORE-ET gets the same force, and its derived sources are
**re-derived**, not quoted:

```
OK  RECOMPUTED derived CORE-ET sources match ee945429d58898f9…
OK  RECOMPUTED consumed derived directory still matches ee945429d58898f9…
```

Final verification of the pilot: **ACCEPTED COVERAGE 2 of 3**, the third
rejected by name for its nonzero emitter rc, `reconciled=YES` alongside it —
row shape and completeness, which is what that flag has always meant.

### 26.4 Fixed-certificate replay

`replay_d4_certs.sh` over the pilot's rows, certificates held fixed by SHA with
nothing re-emitted: **exit 0, 2/2 parsed, compiled and ran, both digests
PRESERVED, `runtime_complete=YES`.**

One bug of mine surfaced here: the preflight runs its child in `formal/lean`,
so a relative output directory resolved against *that* and the redirect failed
with "No such file or directory" — which reads like a missing toolchain rather
than a path bug. `OUTDIR` is made absolute as soon as it is created.

### 26.5 The shift regression probe

Two adaptations, both named in the file. `denoteResidual` takes the clock edge
vector first now; the pinned call predates edges and means every clock fires,
so it is given `allEdges` of the same certificate. And section 5 called a
helper `bv_shl_bounded` that master does not define — the bound lives inside
`eval_op`/`denote_op`, where `sem_shl` characterises it, and adding a
definition so a probe could call it would be introducing library surface for a
test's convenience. The boundary coverage is kept by driving the same guard the
ordinary way: one below the width, at it, far past it with the exponent that
used to panic, and zero width.

**23 `#guard`s, exit 0, zero errors, 0:14.79, 6,662,984 KiB** — including the
three real CORE-ET exponents (8589934576, 8589934368, 8589934496 at widths 66,
258 and 258) whose unguarded `2 ^ k` once killed the whole run.

### 26.6 State

The full **122-module CORE-ET procedure is running** under the same settings,
serial, into `generated/master_port/d4_coreet_122`. The frozen 30 CVA6 is
**not** started: its foundation requires a checkout with no tracked
modifications and all three CVA6 checkouts carry the same three local RTL edits
(§25.6). A passing pilot does not close full coverage, and nothing here is
final integrated acceptance.

## 27. The full 122, and CVA6

### 27.1 CORE-ET 122: dispositions

Serial, optimized `lhd` `9cdba78e6b236e8c` (k8-opt), `EMIT_MAX_RSS_KB`
8,388,608, 3600/1800 timeouts, ordinary **8192 KiB** stack. 122 rows,
`reconciled=YES`.

| | |
|---|---:|
| EMITTED, PARSED, `compilesOk=true`, **RAN**, with a digest | **76** |
| EMIT_FAILED, NOT_RUN | **46** |

**ACCEPTED COVERAGE: 76 of 122.** The verification exits nonzero and names
every one of the 46; none is promoted. Their diagnostics classify as:

| count | cause |
|---:|---|
| 12 | `comb-loop` |
| 6 | `onehot-deferred` |
| 4 | `bitwidth-unbounded` + `comb-loop` |
| 4 | static gates (`const width 46 cannot hold value 1152886320234758144`, and similar) |
| 2 | `concat-lane-width` |
| 2 | `yosys-failed` |
| 16 | rc=6/7 with no JSON code in the compile log |

**Fixed-certificate replay: 76/76 PRESERVED**, `runtime_complete=YES`, nothing
changed, nothing rejected, nothing re-emitted.

### 27.2 Fresh against the retained historical rows — UNRESOLVED

The historical `coreet_122_r2_rows.tsv` is **provenance, not credit**. The two
runs differ, and the differences are **reproducible and unexplained**:

| | fresh | historical r2 |
|---|---:|---:|
| RAN | 76 | 93 |
| NOT_RUN | 46 | 29 |
| runtime status agrees | — | 101 / 122 |
| ran in both, with a digest | — | 74 |
| **identical digest** | — | **55 of 74** |

**A correction to what I first wrote here.** I reported that all 19 changed
digests were designs containing `Op_SHL` (18) or `Op_Sext` (1), and presented
that as the cause. Both halves were wrong.

*The count was wrong.* I classified a certificate by taking the first
whitespace token of every line and testing it for `19`/`23` — but the first
token is numeric in every section, so source tags, output slots and flop
widths were counted as operators. Walking the sections by their declared
counts and reading only the NODE section gives:

| | n | with `Op_SHL` | with `Op_Sext` | with neither |
|---|---:|---:|---:|---:|
| changed | 19 | **17** | 15 | 0 |
| identical | 55 | 39 | 29 | 12 |

*And the inference was wrong anyway.* 39 designs containing `Op_SHL` have
IDENTICAL digests. Presence of an operator in a certificate does not show that
the operator caused a difference, and it certainly does not show that
everything else was preserved. The fresh run differs from the historical one
in its `lhd` binary, its Lean library and its emitter all at once; attributing
the digests to one definition change picked the explanation I already had in
mind.

**These 19 are recorded as unresolved observations.** Settling them needs a
controlled comparison — the SAME certificate and the SAME stimulus through the
old and new semantics, one variable at a time — which has not been run. Until
then nothing is claimed about which answer is right, no semantics are changed
to make digests agree, and the historical inputs and rows stay exactly as they
are.

The guard comments in `LGraphModel.lean` have been qualified to match: one of
them said "the guard is not a semantic change", which is a claim of
equivalence to the unguarded formula on every input that nothing here proves
— the unguarded formula does not even evaluate at the amounts that motivated
the guard. They now say the bound is the bridge's, and that agreement
elsewhere is open.

Of the 19 that ran historically and do not now, 14 are rc=7, 4 are rc=3 (static
gates), 1 is rc=1 (`Concat lane 0 (of 8, MSB-first) driver 'mux_19104' occupies
2 bits but its declared window is 1`). Two that failed historically now run.
These are dispositions, not explanations either.

### 27.3 CVA6: foundation, pilot, frozen 30

Foundation built against the clean pinned checkout —
`cva6-blockfinding`, `6cb20010…`, `cva6_tracked_clean=yes`, target
`cv64a6_imafdc_sv39_hpdcache_wb`, bender 0.32.0, slang 9.1.214, a 353-line
filelist and a 278 MB AST. **28 wrappers generated, 0 not in config, 2 failed,
out of 30 declared.** The manifest pins the revision, config package, filelist,
AST, generator and wrapper set by hash.

Pilot, same settings as CORE-ET:

| module | emit | runtime | shape | digest |
|---|---|---|---|---|
| `alu` | EMITTED | **RAN** | 1589 src, 1982 nodes | `953094210059088727` |
| `btb` | EMITTED | **RAN** | 1059 src, 1037 nodes, 64 flops | `2825082761692869855` |
| `cva6_hpdcache_subsystem` | EMIT_FAILED (rc=6) | NOT_RUN | — | — |

Verification re-derives **every** binding — foundation, filelist, config
package and the declared wrapper corpus all recompute to their recorded hashes,
and `cva6_root_rev` matches the pin. ACCEPTED COVERAGE 2 of 3, the third named.

**The frozen 30 completed** under that foundation, same settings:

| | |
|---|---:|
| EMITTED, PARSED, `compilesOk=true`, **RAN**, with a digest | **21** |
| EMIT_FAILED | 7 — 4 at rc=3 (static gates), 3 at rc=6 |
| NO_WRAPPER | 2 — the two the foundation could not generate |

**ACCEPTED COVERAGE: 21 of 30**, every refusal named. Verification re-derives
the foundation, filelist, config package and declared wrapper corpus to their
recorded hashes. **Fixed-certificate replay: 21/21 PRESERVED**,
`runtime_complete=YES`, nothing changed, nothing rejected.

The two `NO_WRAPPER` modules are the foundation's own `wrappers_failed=2`
carried forward rather than hidden: a module with no wrapper is an unresolved
prerequisite and is recorded as one, not as a design that failed.

### 27.4 What none of this is

Finite seed-42 all-edges execution. The differential half did not run. A
passing pilot does not close full coverage, `rowparts` counts are progress and
not accepted coverage, and no part of this is final integrated master
acceptance.

## 28. The D3 per-design pilot

### 28.1 The designs, and why these

**Two real CORE-ET exports and one supplied fixture.** `tiny_sync_ram` is a
fixture, not a benchmark design, and the three must not be described as if they
were all real exports. Staged with recorded input hashes in
`generated/master_port/d3_pilot/certs/`:

| design | role | shape | input sha256 |
|---|---|---|---|
| `intpipe_alu` | combinational | 1536 sources, 2133 nodes, 0 flops, 0 memories | `16703b19dca910d1…` |
| `txfma_f4` | **stateful** | 750 sources, 1867 nodes, 6 flops, **2 clock domains** | `014b5c9983b32f70…` |
| `tiny_sync_ram` | memory addendum — **supplied fixture, not an export** | 6 sources, 8 nodes, 1 flop, **1 memory** | `861d069e45936b82…` |

`txfma_f4` carries `ctrl_f3_clk` and `ctrl_f4_clk` with **three flops on each**
— real post-D2 clock transport from the scanner, not a fixture. The memory
addendum is there because **no design in either corpus that reached RAN carries
a `MemoryDesc` at all**: `tiny_sync_ram` is the smallest real emitted design
that does, and it is labelled as an addendum rather than counted as one of the
two.

The run needed `ReifyGen`, `ReifyProof`, `D3Harness` and `Reify` built in the
tracked library first — 2:46.82, 7,140,328 KiB, exit 0. The first attempt
failed every target at `cert` with *"object file … ReifyGen.olean does not
exist"*, which is the correct refusal for an unbuilt dependency.

### 28.2 `legacy` reifier: all three reach `agree`

| design | verdict | selftest_base | mut_out | mut_flop | mut_mem | distinct obs | wall | peak RSS |
|---|---|---|---|---|---|---:|---:|---:|
| `intpipe_alu` | **agree** | 1 | **rejected** | na | na | 30 | 40.15 s | 2,129,808 KiB |
| `txfma_f4` | **agree** | 1 | **rejected** | **rejected** | na | 32 | 20.22 s | 1,906,824 KiB |
| `tiny_sync_ram` | **agree** | 1 | **rejected** | **rejected** | **rejected** | 32 | 3.26 s | 1,519,060 KiB |

The mutation columns ARE the changed-executable-output and changed-state
controls, and they are part of the original procedure rather than something
added for this pilot. Every applicable mutation was **rejected** by the
checker.

An inapplicable one is distinguishable from a pass: `mut_mem=na` on
`intpipe_alu` and `txfma_f4` is paired with `mutable_mem=0`, and `mut_flop=na`
on `intpipe_alu` with `mutable_flop=0`. A design with no memory cannot have its
memory mutated, and the row says so rather than scoring a success.

`proof=na` throughout — `--prove` was not requested on this pass, and the
column stays `na` wherever it was not attempted.

### 28.3 Two defects the pilot exposed

- **An exploratory row did not bind its input** (§20.3 below): every
  `--certs` target was built with an empty `cert_sha256`, and `--resume`
  compares that against the target's hash — both empty, so a **changed
  certificate was silently reused**. Measured on the committed sweep:
  `reused=1, rc=0`.
- **The phase options' help named a narrower requirement than their guards.**
  `--phase-only` and `--phase-split` both said "Requires `--reifier named`"
  while the guards accept `named` **or** `shared`. Reading the help, I added a
  check enforcing named-only — which would have removed a supported
  combination on the strength of a doc string. Reverted; the guards are
  untouched and five strings now say what the code does.

Refusals measured on the actual CLI after that correction: `named` and
`shared` accept both phase options; `legacy` and `chunked` refuse with exit 2
naming the reason; `--phase-only --prove` refuses; `--proof-backend module`
without `--reifier chunked` refuses; `--prove --reifier shared` refuses
("no proof bridge yet, so --prove would credit a model that no theorem
covers"). An unsupported combination is a **refusal with a reason**, never a
row.

### 28.4 All four reifiers

| reifier | `intpipe_alu` | `tiny_sync_ram` | `txfma_f4` |
|---|---|---|---|
| `legacy` | agree (40.15 s, 2,129,808 KiB) | agree (3.26 s) | agree (20.22 s) |
| `named` | **timeout** (1803.11 s) | agree (7.17 s) | agree (42.38 s) |
| `shared` | agree (28.76 s, 1,826,940 KiB) | agree (3.38 s) | agree (16.85 s) |
| `chunked` | agree (24.60 s, 1,828,368 KiB) | agree (2.97 s) | agree (13.52 s) |

**11 of 12 reach `agree`.** Every applicable mutation is `rejected` in every
one of those 11 rows, and every `na` is paired with its `mutable_*` column at
0, so an inapplicable mutation is never scored as a pass.

The one exception is a **TIMEOUT, which is undecided and not a failure**:
`named` on `intpipe_alu` (2133 bindings) ran the full 1800 s per-target bound
and was killed inside `report`, after `residual_ready`, at ~2.2 GB. The other
three reifiers finish the same design in 25–40 s. That is a characterisation
of `reify_design_named`'s executable stage at this size, not a verdict about
the design, and the row says `timeout` with no observation counts.

### 28.5 Phase diagnostics

`--phase-only`, all six components on `tiny_sync_ram`, `--reifier named`, each
alone in a fresh process:

| component | result |
|---|---|
| `none` | (no component; residual only) |
| `selftest` | `selftest base=true` |
| `agree` | `agree=true samples=32` |
| `distinct` | `distinct=32 samples=32` |
| `fast1` | `fast1 obs=510` |
| `fast2` | `fast2 obs=512` |

Every one stops at **`verdict=typecheck`**, which is what the flag promises:
no `report` call is made, no `D3GATE` line is printed, and the row **cannot be
credited**. A diagnostic run is visibly not a passing one. `fast1` and `fast2`
differ (510 vs 512 observations), so the comparison the gate makes is not
vacuous.

`--phase-split` on both supported reifiers reaches **`agree`** — `named` and
`shared`, `sim_one_result obs=510` in each — because it runs the components
one at a time and *then* calls `report` unchanged. The contrast with
`--phase-only` is exactly the documented one.

### 28.6 `--prove`, on all three backends

| backend | design | result |
|---|---|---|
| `legacy`, in-process | `tiny_sync_ram` | **proof=0** — refused |
| `named`, in-process | `tiny_sync_ram` | **proof=1** |
| `chunked`, module | `tiny_sync_ram` | **module_proof=0**, `group_build_failed` |

**`named` closes a stateful design.** `prove_reified_incr: d3_fast.correct
proved over 8 named value(s), 3 terminal slot(s), max live cut 4`, and

```
D3GATE proof=1 thm=d3_fast.correct axioms=[propext, Classical.choice, Quot.sound]
```

Standard axioms only, on a design with a flop AND a memory — post-D2 transport
exercised by the theorem, not just by the executable. 12.37 s, 6,849,492 KiB.

**`legacy` does not**, and refuses rather than reporting success:

```
prove_reified: the generated `d3_fast.correct` depends on DISALLOWED axiom(s)
[sorryAx]; allowed: [propext, Classical.choice, Quot.sound]. The theorem did
not close -- refusing to report success.
```

The unsolved goal is a `flopNextV …` equality — the **flop update**. So
`prove_reified` does not close the commit side on this design, and the gate
catches it. `proof=0` with a named cause, not a silent pass.

**The module backend refused in the ordinary session**, and says so:

```
d3_guarded_build: REFUSING -- no delegated cgroup v2 base. The build would run
unguarded, which is what this wrapper exists to prevent.
```

**I then concluded that the module backend "cannot run on this host". That
was wrong.** It runs under

```
systemd-run --user --scope --quiet --property=Delegate=yes
```

where the sweep's own `cgroup_base()` returns
`/sys/fs/cgroup/user.slice/user-1008.slice/user@1008.service`. My check tested
whether one cgroup's `cgroup.subtree_control` was WRITABLE — not what
`cgroup_base()` asks — and I ran it from the ordinary session rather than
inside the scope, so I measured the wrong thing in the wrong place and
generalised from it. §29 has the run that works.

### 28.7 What this does and does not close

Three designs. **Nothing here closes the per-design D3 obligations for the 122
CORE-ET or frozen-30 CVA6 corpora**, the published scaling sweeps, the 38 FP
modules, or the final four-mode validation. The `--prove` coverage is one
design on two working backends.

`txfma_f4` under `named --prove` reached **88,118,504 KiB** (84 GiB) at 953 s
and I terminated it: `proof=0`, caller exit 143, with the executable agreement
(`verdict=agree`) retained. The time file's trailing `exit status 0` does not
override the signal-15 termination. That is **unproven after a resource stop**,
not a semantic counterexample.

I ran it without `--kill-over-rss-kb`, which defaults to **0** — no hard limit
— while `--max-aggregate-rss-kb` is a SOFT cap that stops LAUNCHING new work
and does not terminate a running probe. Nothing was going to stop it. The guard
existed and I did not arm it.

## 29. Module-proof admission, the guarded route, and the original tests

### 29.1 Admission: three wrong results were being credited

`row["module_proof"]` was `1 if res.get("status") == "proved" else 0` and
nothing else, so each of these was credited, all reproduced against this CLI:

- a worker that printed a proved result and then **exited 17**;
- a **stale** `result.json` from an earlier attempt, this run producing none;
- a result naming a **different certificate** — the row carried the correct
  `cert_sha256` beside an all-zero `module_cert_sha256` and still said
  `module_proof=1`.

The driver's own checks (gate line, rc, allowed axioms, olean drift) are
untouched and are still what `status == "proved"` means. What is added is the
boundary around them: any pre-existing `result.json` is removed before the
worker runs, the worker must exit 0, and the result must name **this**
certificate. A new **`module_admit`** column carries that decision, separate
from `module_proof_status`: one says whether the verdict may be credited, the
other what the driver concluded.

`d3_module_admit_controls.sh`, nine legs, all passing; **against the committed
sweep 7 of 8 fail**. Every refused leg also checks the earned `cert..agree` is
preserved — a refused proof must not retract an agreement.

### 29.2 The guarded route — correcting §28.6

I wrote that the module backend "cannot run on this host". **It runs**, under

```
systemd-run --user --scope --quiet --property=Delegate=yes
```

The RAM fixture, through the original chunked procedure with its guard and
caps intact:

```
prove_reified_chunked: d3_fast.correct proved over 8 binding(s) in 1 chunk(s) of at most 8
D3GATE proof=1 thm=d3_fast.correct axioms=[propext, Classical.choice, Quot.sound]
```

`module_proof=1`, `module_admit=ok`, `gate_axioms_within_allowed=True`,
`olean_drift=False`, 23.33 s, 6,834,076 KiB, cgroup peak 779,332 KiB, caps
`group_max_kb=10,000,000` and `final_max_kb=14,000,000`.

**`txfma_f4` is not proved by any backend.** In-process it reached 84 GiB and
I stopped it (§28.7). Through the module backend, **all 16 group builds
succeeded** (rc=0, max group RSS 8,749,720 KiB under the group cap, 59 chunks
of 32) and then the **composition was killed at a cgroup peak of exactly its
14,000,000 KiB `final_max_kb`**, `rc=137`, no gate line emitted. Both failures
are **resource stops, not semantic results**, and the design remains unproven
rather than disproved.

An earlier attempt at the same run failed differently — 7,090 Mathlib
`.olean.server` replay errors — because **I ran the restored tests
concurrently against the same Lake workspace**. Two Lake builds in one
workspace. The files were all present afterwards.

### 29.3 A memory stop was labelled as a disproof

`d3_module_proof.py` separated `timeout` and SIGTERM-`killed` from
`not_proved`, with its own comment giving the reason: a reviewer needs to know
"whether the gate was evaluated and disagreed, or was never reached". It did
not separate **rc 137**, and `d3_guarded_build.py` returns exactly 137 when the
**cgroup OOM-killed** the command — so that status is this run's own guard
reporting a stop, not a signal to be guessed at.

`txfma_f4`'s composition therefore came back `not_proved`, which says the gate
disagreed. It was never reached. The status is now **`rss_killed`**, with a
ninth control leg requiring it to be refused AND named as a resource stop. Both
always gave `module_proof=0`, so nothing was overclaimed — the defect was the
label, and the label is what gets read.

### 29.4 The five missing original tests

At the pinned D3 revision `78a3e0d8c10c3e1ce1627d8d54fa898ee4416d43`,
`d3_proof_test.py`, `d3_runner_test.py`, `d3_scheduler_test.py`,
`d3_sweep_cancel_test.py` and `d3_sweep_parser_test.py` exist; **all five were
absent from master**, and the installed `d3_cgroup_test.py` exited 1 before
testing because it imports the runner test. Restored at their pinned hashes.

| test | result |
|---|---|
| `d3_sweep_parser_test` | **OK** |
| `d3_sweep_cancel_test` | **OK** |
| `d3_runner_test` | **OK** |
| `d3_cgroup_test` | **OK (14 skipped)** — previously exited 1 before testing |
| `d3_proof_test` | **OK** after three adaptations |
| `d3_scheduler_test` | **one check failing** |

`d3_proof_test`'s adaptations, each preserving the assertion: its fixture
certificate was a hardcoded path into another checkout AND pre-clock —
master's reifier emits `d3_fast` with the edge vector first, which is exactly
the arity mismatch it hit — so the path, module name and reifier are inputs
now with project-local defaults, and the real-Lean legs **skip, reported as
skips rather than passes**, when the certificate is absent; the end-to-end leg
uses the reifier that proves a stateful fixture, since `legacy` leaves
`sorryAx` on a `flopNextV` goal; and `residual_bound_once_before_report`
matched the literal `d3_fast R 32` where master's call is clock-aware, so it
now matches the report call and still requires `R` to be the argument after the
model — both halves, neither relaxed.

### 29.5 The restored test found a real defect in the sweep

`d3_scheduler_test`'s **`hard_kill_terminates_running`** failed, and the cause
turned out to be in `d3_sweep.py`, not in the test.

The hard-limit branch announced *"terminating the running probe(s)"*, set
`_RSS_KILL` and `_RSS_STOP`, reaped, and returned — **all of it
unconditionally**. When the trip happens BEFORE the first probe launches,
which it does whenever the driver's own RSS already exceeds the limit, there is
nothing in `_GROUPS` to reap. The monitor then exits, `_RSS_STOP` defers the
rest of the queue, and the first probe launches afterwards and runs
**unwatched**.

Reproduced directly, 1 kB limit, 3-second probe:

```
d3_sweep: AGGREGATE RSS 41964 kB reached the HARD limit 1 kB -- terminating the
          running probe(s).
[1/3] blk00_gate      agree      3.05s rss=11028kB
```

The line is printed, nothing is killed, and the probe runs its full three
seconds to `agree`.

**Repaired**: `_reap_all()` returns how many groups it terminated, and the trip
is latched only when that is non-zero. Otherwise the monitor logs once and
**keeps sampling**, so the next probe to launch is the one it stops. This can
only ever kill MORE, never less — it does not weaken the resource policy, it
makes it do what it already claimed.

After the repair, same reproduction:

```
d3_sweep: AGGREGATE RSS 42024 kB is already over the HARD limit 1 kB with NO
          probe running -- nothing to terminate yet, so the limit is not latched
          and the monitor keeps watching. (logged once)
d3_sweep: AGGREGATE RSS 54968 kB reached the HARD limit 1 kB -- terminated 1
          running probe group(s).
  blk00_gate  run_status=rss_killed  verdict=rss_killed  launched=1
  blk01_gate  run_status=deferred    launched=0
  blk02_gate  run_status=deferred    launched=0
```

**`SCHEDULER-TEST OK`.** And the two neighbours that had been passing
**vacuously** over an empty `killed` set — `hard_kill_launched_not_failed` and
`hard_kill_resumable` — now have something to check.

This is what restoring the original procedures is for: the defect was in
master's sweep, had been there through every run in this port, and only a test
that asserts the guard ACTS could find it. My first guess at the cause was a
race between a fast probe and the sampler; the test's own `STUB_DELAY=3` ruled
that out, which is why it was worth reproducing rather than assuming.

### 29.7 Everything re-run against the repairs

`txfma_f4`'s module composition, re-run after the status fix: **`rss_killed`**,
`module_admit = "refused: worker exit 1; driver status rss_killed"`, cgroup
peak exactly 14,000,000 KiB again, 316.5 s. Reproducible, and now labelled as
the resource stop it is rather than as a disproof.

With nothing else building, every suite touched in this port:

| suite | result |
|---|---|
| `d3_sweep_parser_test` | OK |
| `d3_sweep_cancel_test` | OK |
| `d3_runner_test` | OK |
| `d3_cgroup_test` | OK (14 skipped) |
| `d3_proof_test` | OK |
| `d3_scheduler_test` | OK |
| `d3_module_admit_controls.sh` | OK (9 legs) |
| `d3_resume_controls.sh` | OK (5 legs) |
| `d3_bench_generation_controls.sh` | OK (10 legs) |
| `//pass/lean:lean_export_tests` | 4/4 PASSED |
| `exit_status_controls.sh` | OK (11 legs) |
| `verify_binding_controls.sh` | OK (12 legs) |
| `wrapper_deps_controls.sh` | OK |

The `d3_scheduler_test` row is the one that matters: it was the failing check
that exposed §29.5's defect, and it passes now with `killed` non-empty, so the
two neighbours that had been vacuous are doing work.

### 29.8 Admission bound to the selected input, and to freshness

Three further ways the module result was credited wrongly, each reproduced
against the actual CLI before the repair:

- **A preseeded result the run could not remove.** The removal sat inside a
  bare `except OSError: pass`, so a directory mode of `0555` was enough: the
  old file survived, a worker that created nothing exited 0, and that result
  was read and credited. Freshness is now **established, not attempted** —
  `clear_stale_result` reports why it failed and the run refuses, because it
  cannot tell its own output from someone else's.
- **A worker that rewrote its own input.** `want_cert` was hashed AFTER the
  worker ran, so a worker could append a comment to the certificate and submit
  the new digest: the result matched, the row still named the old input, and
  the proof was credited over byte drift. The identity is now the one
  **selection bound**, and the certificate is re-hashed afterwards to confirm
  it is still those bytes.
- **Malformed results erased earned work.** `json.loads` returns whatever the
  file holds; a list or a scalar made `res.get(...)` raise `AttributeError`
  and a non-string `cert_sha256` made the slice raise `TypeError`, and both
  propagated out of the proof stage and **destroyed the `cert..agree` the
  executable stage had already earned**. `read_module_result` validates
  structure and types first, so a malformed result costs the proof credit and
  nothing else.

The decision now lives in three functions — `clear_stale_result`,
`read_module_result`, `admit_module_result` — which `run_one` calls and a
control can drive directly.

### 29.9 The aggregate cap now reaches the module worker

§29.6 recorded that the sampler **counted** the module worker while
`_reap_all()` could never terminate it. Measured end to end: a bounded 80 MiB
module child under a 90,000 KiB sampled cap was counted at 131,264 KiB, the
sampler reported no probe running, the child completed, and `module_proof=1`
was credited over the cap.

The worker is now launched through **`run_group`**, the sweep's own mechanism:
its own session, its pgid registered in `_GROUPS`, so the whole group — the
worker, its `lake`/`lean` stages and anything they spawn — is terminated
together, with `_reap`'s bounded grace and `_sweep_orphans` as the last
resort. **The per-group and final caps inside the worker are untouched**; this
adds the aggregate one, which was already supposed to apply. No cap value
changed and no counted memory is hidden.

### 29.10 Memory attributed on evidence, not on the exit status

§29.3 replaced `not_proved` with `rss_killed` for `rc == 137`. That was still
too coarse. `d3_guarded_build.py` returns 137 when the kernel killed something
**and** passes a command's own 137 straight through, so a child that simply
exits 137 reports `oom_kill=0` and was being labelled exactly like a real OOM
at the cap.

The driver now parses `oom_kill=` from the guard's own line — the kernel's
counter from `memory.events` — and records it as `final_oom_kill`.
`rss_killed` requires `rc == 137` **and** `oom_kill > 0`; a 137 without it is
`killed`, which is what 143 already meant. The real `txfma_f4` composition had
`oom_kill=3` at a 14,000,000 KiB cap with no proof gate, so it keeps
`rss_killed` and remains **unproven after a resource stop**.

### 29.11 Controls, isolated

`d3_module_admit_controls.sh` was rewritten. The previous version **overwrote
the live `d3_module_proof.py`** and restored it from a trap, and its stale leg
preseeded one directory while the run chose another — so that leg passed
without ever reading a stale file.

It now drives `clear_stale_result`, `read_module_result` and
`admit_module_result` in the **live module, imported and never written to**,
against real filesystem state: actual files, an actual `0555` directory, an
actual mutated certificate copy, each in its own context. Two alternatives
were tried and rejected: a copied sweep cannot resolve its Lean root (it looks
for `generated/formal/lean`), and adding a worker-path seam to production code
for a test is what `d3_sweep.py`'s own note argues against.

**19 legs, all passing**, including that the live sweep, the live worker and
the source certificate are byte-identical afterwards. End-to-end CLI evidence
is kept separately and not duplicated: the RAM fixture gives `module_proof=1
module_admit=ok` through the real backend (re-confirmed after these changes —
13.46 s, cgroup peak 779,644 KiB), and `txfma_f4` gives `rss_killed` with the
proof refused and `verdict=agree` retained.

All six restored D3 tests and the other control suites still pass after the
change.

### 29.12 Real-CLI negative controls, through the process boundary

The 19 helper legs test the decision; they do not test the worker/process
boundary or the row that gets written. `d3_module_cli_controls.sh` does, over
**17 legs on the actual command line**, asserting on the ROW each run produces
and on the earned `cert..agree` being retained whenever proof admission
refuses.

Isolation: a private context per case -- its own `scripts/` (a copy of the
sweep and a stub worker), its own certificate copy, its own `--out`. Nothing
live is written to, and the suite verifies the live sweep, live worker and
source certificate are byte-identical afterwards.

**Correcting §29.11.** I wrote that a copied sweep "cannot resolve its Lean
root (it looks for `generated/formal/lean`)". It resolves it perfectly well
when told where it is: `D3_LEAN_ROOT` is an existing seam and the whole suite
rests on it. `D3_RUN_ID` is the second seam that matters -- set per case, the
run directory is stable and known in ADVANCE, which is what makes the stale
and failed-unlink legs stageable **at the exact path the run reads** instead
of at a guess.

Legs: positive success; nonzero-after-proved; stale; failed unlink; input
changed under the worker; missing, malformed and list-shaped schema; a hung
worker; and the over-cap stop with its resume.

### 29.13 Three ordering defects the over-cap leg exposed

Sizing it correctly took three corrections, each a real defect rather than a
test artefact:

- **The cap was tripping at the wrong stage.** At 90,000 KiB the SIM probe
  tripped it at 132,728 KiB and the module worker never ran. The stages are
  sequential, so the aggregate during the module stage is the worker ALONE --
  the sim probe's memory is already freed. The worker must out-allocate the
  sim probe (~1,520,000 KiB here) for the cap to catch it where the leg aims.
- **The sampled-stop block was overwritten.** It sat BEFORE the generic
  `module_admit` and `module_proof_status` assignments, which then replaced
  its values. Moved after them.
- **The sampler reaped before it recorded.** `_reap_all()` ran, then
  `killed_at_kb` and `_RSS_KILL` were written -- so a terminated stage could
  return, be classified and have its row written before the trip's record
  existed to read. The module stage hit exactly that: the worker came back
  `-15`, the row looked for the record, found none, and wrote an ordinary
  nonzero exit with `run_status=done`. **The record now precedes the reap**,
  with the "only latch when something is running" property kept by checking
  `_GROUPS` rather than the reap count.

And one consequence of §29.9's own change: a `module_stopped` row was excluded
from verdict recomputation, so `verdict` came back **EMPTY** -- the earned
`agree` was not retracted so much as never written. `module_stopped` now joins
`done` there, and only there, because it is the one nonterminal status whose
gates are real.

### 29.14 An outer bound on the module worker

`--module-proof-timeout` bounds the composition INSIDE the worker. Nothing
bounded the worker itself, so a hang anywhere earlier ran unbounded. The call
now carries twice the inner bound plus five minutes -- generous enough that it
can never fire before the mechanism it backs up -- and `run_group` returns 124,
which admission refuses like any nonzero exit. Covered by the hung-worker leg.

### 29.15 Owned cleanup, completed and verified

`d3_cgroup_reclaim.py` takes candidates ONLY from the `composition.cgroup`
records the runs wrote, refuses any path that is not an owned `d3build_*`
cgroup, refuses any that is still populated, retries bounded, and **re-checks
each path afterwards** rather than reporting success from a call returning.

Both recorded leftovers reclaimed -- `d3build_1688839` and `d3build_3071143`,
each `populated=0` with no processes -- two others already gone, **residual=0**,
verified independently after the run.

All six restored D3 tests and all four control suites pass after every change
above.

### 29.6 The aggregate limit counts memory it cannot terminate

Observed on the `txfma_f4` module run, and verified rather than inferred:

```
d3_sweep: AGGREGATE RSS 16387340 kB reached the HARD limit 16000000 kB --
          terminating the running probe(s).
```

— while `d3_module_proof.py` went on running for another 30 minutes.

The two halves disagree because they look at different sets. `_marked_pids()`
counts every process whose command line carries `_MARK`, this run's directory,
and the module worker's `--out-dir` is inside it, so its memory **is** in the
aggregate (checked directly against `/proc/<pid>/cmdline`). `_reap_all()`
terminates `_GROUPS`, which only `run_group()` populates — and the module
worker is launched by a plain `subprocess.run`, deliberately outside the
probe's process group and cgroup, because it is many processes with different
budgets. It is never in `_GROUPS`, so it can never be reaped.

So the aggregate limit **trips on memory it does not own**. That memory is not
unguarded: `d3_guarded_build` enforces `group_max_kb` per group and
`final_max_kb` on the composition, and those demonstrably act — §29.3's
composition was killed at exactly its 14,000,000 kB cap. But the aggregate
monitor's announcement was wrong about what it had done.

The §29.5 repair makes this honest rather than silent: with nothing in
`_GROUPS` to reap, the run now logs *"already over the HARD limit … with NO
probe running — nothing to terminate yet"* and keeps sampling, instead of
claiming a termination and exiting. **Giving the aggregate limit authority over
the module worker is a policy change and is NOT made here**; what is recorded
is that the two caps govern different things, and which one acts on the
composition.

## 20. D3 replay backlog — repaired

Both gaps are closed, and running them found two more.

### 20.1 `d3_gen.py`: the edge arity, and two defects behind it

`denoteResidual` takes the clock edge vector first; the generator emitted the
three-argument call, so **every** scaling output failed to typecheck. The
synthetic program has no flops and no memories, so no edge vector can change
its answer — which is why the emitted theorem now quantifies over **every**
`e` rather than fixing one: the stronger statement is also the easier one
here, and fixing an `e` would quietly narrow what the scaling numbers are
about.

Fixing that exposed two more, both only reachable on the `far` shape:

- **A repeated operand slot.** `deps(1, "far")` is `(2, 2)`, so both operands
  read the same binding. The proof emitted two `have`s for it and rewrote with
  both; the first `rw` had already replaced every occurrence, so the second
  died with *"Did not find an occurrence of the pattern `refBV E1 2`"*. One
  `have` per DISTINCT slot now. `chain` never repeats a slot, which is why it
  was never seen.
- **`clear` of a name that was never introduced.** The pruned variant cleared
  `rb{k}` unconditionally — an unknown-identifier error once `rb{k}` is only
  emitted when the operands differ.

**All six variant × shape combinations typecheck**, exit 0, zero errors:

| | chain | far |
|---|---|---|
| naive | n=2, n=4, **n=8** (0:13.97) | n=4, **n=8** (0:14.53) |
| composed | n=4, **n=8** (0:17.52) | n=4, **n=8** (0:19.72) |
| pruned | n=4, **n=8** (0:23.04) | n=4, **n=8** (0:18.54) |

An earlier version of this table said "all six through n=8" while only four
n=8 runs were retained. The two missing ones — naive × chain and naive × far
— have since been run; the claim and the retained files now agree.

### 20.2 `d3_bench.sh` / `d3_bench2.sh`

- **Scratch is project-local.** `mktemp /tmp/…` is shared between parallel jobs
  on this box, is not the configured runtime area, and does not survive a
  rebuild. Now `$SCRATCH`, under `generated/`.
- **The status comes from the exit code.** It was `OK` unless the exit was 124
  or the log matched `^probes.*error`, so any other nonzero exit — a missing
  module, a killed process, a Lake error not starting with `probes` — was
  recorded as a PASSING measurement with whatever CPU figure the partial run
  produced. Now 0 is OK (and still FAIL if the log carries an error), 124 is
  TIMEOUT, anything else is `FAIL(rc=N)`.
- **The sweep points and the output path are overridable.** A scaling script
  whose only mode is a multi-hour run cannot have its own status logic checked
  without paying for a full sweep — which is how the defect above survived.
  The published points remain the defaults; an overridden sweep is a harness
  run, not a measurement.
- **Generation is part of the measurement.** `run()` discarded the generator's
  exit status and never checked that it had produced anything, so when
  generation failed the harness measured whatever `probes/<name>.lean` was
  left from an earlier run — and reported `OK`, because a stale artifact still
  typechecks. A scaling number attached to the wrong `n` is worse than a
  missing one. The artifact is now removed first, the generator must exit 0, a
  non-empty file must exist afterwards, and its hash goes in the row;
  `gen.log`, `gen.exit`, `lean.log`, `lean.exit`, `lean.time` and
  `artifact.sha256` are retained per row. A failed generation takes the status
  column as `GEN_FAIL(rc=N)` with `cpu_s` `NA`, so it can never read as a slow
  pass.

- **The baseline row was never taken.** `timeit` grew a row-directory
  argument and the baseline call did not, so under `set -u` the command
  substitution aborted with `$2: unbound variable`: the baseline child never
  ran, `BT`/`BS` came back empty, and the script wrote a **five-field blank
  row beneath a six-field header** and exited 0 — a zero subtracted from every
  measurement, with nothing to show it was never taken. The baseline now has
  its own evidence directory, writes a fresh artifact, records its hash and
  emits a complete row through the same path as every other row.

`d3_bench_generation_controls.sh` drives the actual CLI over ten legs,
including a stale artifact pre-seeded at the exact path the row would use, and
the baseline's success, nonzero-child and timeout paths.
**Non-vacuity measured twice**: against the committed `d3_bench2.sh` the
generation legs fail 5 of 6 — the stale leg reports `OK`, which is the defect
— and against the committed `d3_bench.sh` all four baseline legs fail, the row
coming back with five fields and a blank status. The repaired scripts pass 10
of 10. The controls swap a failing stub in for `d3_gen.py` and restore it from
a `trap`, and a leg checks the generator is byte-identical afterwards.

Smoke run at n=4, three points, all `OK`, **zero files left in `/tmp`**.
**Non-vacuity measured**: with `ELAN_HOME` pointed at a stub `lake` that exits
17, every row comes back `FAIL(rc=17)` with `0.00` CPU — where the old logic
would have recorded `OK`.

### 20.3 Still open

The **full scaling sweep** (n up to 4096 chain, 1024 far) has not been run —
it is a multi-hour measurement and the 122-module CORE-ET procedure has the
machine. The generator and harness are repaired and exercised; the published
scaling NUMBERS are not yet reproduced.

## 31. Why `scoreboard_gate` timed out: `bv_bitwise` costs 3.5 microseconds per bit

The frozen-30 master sweep returned 20 `agree` and one `timeout`. The timeout
is `scoreboard_gate`, at the 3,600 s bound. It is NOT a hang, a loop or a wrong
answer -- the row is `timeout`, which is undecided -- but it is also not just
"a big design", and chasing it found two real defects.

### 31.1 Where the time goes, measured by cutting the probe down

The probe was split into a ladder and each prefix elaborated on its own, with
`decoder_gate` as the control (same corpus, same toolchain, no wide
operations). Seconds, and peak RSS:

| prefix | scoreboard_gate | decoder_gate |
| --- | ---: | ---: |
| certificate literal only | 31.9 s / 2.31 GB | 66.7 s / 1.97 GB |
| + `_step` | 32.0 s / 2.37 GB | 37.6 s / 2.01 GB |
| + `reify_design` | 260.7 s / 5.90 GB | 84.5 s / 2.64 GB |
| + `d3_residual` | 270.8 s / 5.89 GB | 73.8 s / 2.65 GB |
| + `#eval report` (whole probe) | **3603.1 s, killed** | 80.1 s / 2.65 GB |

So `#eval report` is at least **3,332 s of the 3,603** -- 92 % -- while the same
step costs the control about 6 s. The certificate literal is NOT the problem:
scoreboard's is 2.5x the bytes of decoder's and elaborates in half the time.
(The decoder column wanders by 2x between prefixes that are strict subsets of
one another; those runs shared the machine with the sweep. The scoreboard
column is internally consistent and the 92 % figure does not depend on the
noise.)

### 31.2 `report` evaluates the whole design 98 times

Per `D3Harness.report`: one `fast` for `sim_exec`, 32 `fast` + 32
`denoteResidual` for `agree`, 32 `fast` for `distinctObservables`, and one
`denoteResidual` for `checkerSelfTest`. That is **65 `fast` and 33
`denoteResidual` passes**, by design -- sampled differential evidence is what
the gate is.

### 31.3 The cost of one pass is set by BIT WIDTH, not by node count

A width census over the 21 master CVA6 certificates, against the measured wall
time:

| block | nodes | ops >= 1000 bits | max width | total declared width | wall |
| --- | ---: | ---: | ---: | ---: | ---: |
| `scoreboard_gate` | 5,170 | **2,537** | 3,868 | **8,804,743** | timeout |
| `bht_gate` | 4,832 | 0 | 385 | 791,184 | 614.9 s |
| `btb_gate` | **1,037** | 346 | 2,080 | 741,138 | 328.5 s |
| `store_unit_gate` | 1,346 | 0 | 561 | 185,738 | 86.7 s |
| `decoder_gate` | 3,714 | 0 | 439 | 55,097 | 88.5 s |
| `aes_gate` | 3,569 | 0 | 65 | 31,793 | 68.9 s |

`btb_gate` is the clean refutation of node count: a THIRD the nodes of
`decoder_gate` and four times the wall time, because it carries 346 operations
of 2,080 bits. `scoreboard_gate` is eleven times the next design on total
width, and its widest operations are 3,868-bit `Op_SHL`.

### 31.4 The primitive: 13.55 ms for one 3,868-bit xor

`bv_bitwise` is bit-at-a-time (`Translation/LGraphModel.lean`):

```lean
def bv_bit (x : BV) (i : Nat) : Bool := ((bv_uint x).toNat >>> i) % 2 = 1
def bits_to_int (w : Nat) (f : Nat -> Bool) : Int :=
  ((List.range w).filterMap fun i => if f i then some (2 ^ i : Int) else none).sum
def bv_bitwise (w : Nat) (f : Bool -> Bool -> Bool) (a b : BV) : BV :=
  mk_bv w (bits_to_int w fun i => f (bv_bit a i) (bv_bit b i))
```

Each bit allocates: a `bv_uint` big-integer modulo per operand, a shift, a
`2 ^ i`, a list cell, and a term in the sum. Measured, 20 iterations each:

| width | per operation |
| ---: | ---: |
| 64 | 550 us |
| 256 | 1,300 us |
| 1,024 | 3,250 us |
| 2,080 | 7,000 us |
| 3,868 | **13,550 us** |

About 3.5 us per bit, roughly linear in width with an enormous constant. One
3,868-bit xor costs 13.55 ms; `rshlV` calls `bv_bitwise` once per extra
dependency, so scoreboard's 2,537 wide operations put a single evaluation pass
at order 25 s, and 98 passes at 2,400-3,300 s. That is the measured
`>= 3,332 s`, from the primitive, with nothing else needed to explain it.

At 64 bits it is already about 8.6 us per bit, against `Nat.xor`/`Nat.land`/
`Nat.lor`, which are native and O(w/64).

### 31.5 Two defects, and what they are not

**Not** a correctness defect, not a hang, and not a resource cap: nothing was
killed by memory, the peak was 5.83 GiB against a 20,000,000 kB cap, and the
disposition `timeout` already says undecided.

1. **`bv_bitwise`, `bv_not` and `bits_to_int` are the wrong shape.** The
   obvious repair -- `Nat.xor`/`land`/`lor` on the underlying value -- is NOT a
   drop-in: these are semantic primitives and every proof that unfolds them
   would have to follow, and an `@[implemented_by]` fast path would put an
   unverified implementation on BOTH sides of the D3 comparison, where a wrong
   one makes the two sides agree wrongly. The honest repair is a proved
   equality (`Nat.testBit` lemmas) and then the swap. Recorded, not taken: this
   is an evaluator change and it needs its own acceptance.
2. **The sweep sizes targets by node count, and node count under-predicts the
   tail by an order of magnitude.** `--tier` and the per-target `--timeout`
   both derive from `nodes`. Measured over the 96 targets that completed
   (`pass/lean/tests/d3/d3_cost_census.tsv`), a log-log fit gives
   `wall ~ nodes^0.45`, R^2 = 0.612, and its worst under-predictions are
   `btb_gate` at **14.9x** and `bht_gate` at **14.0x** -- the two widest designs
   in the corpus. The same fit on total declared width gives
   `wall ~ width^0.41`, R^2 = **0.705**, and its worst under-prediction is
   6.7x.

   **Correcting the obvious overstatement before it is made:** width is not
   simply "the right metric". By RANK correlation node count is marginally
   better (Spearman +0.910 against width's +0.864), because the ordering is
   dominated by sixty small designs sitting on a ~3 s floor where nothing
   distinguishes them. What width is better at is MAGNITUDE, and a timeout is a
   magnitude question -- does this fit in 3,600 s -- which is why the bound
   should not be derived from nodes. Neither metric is sufficient alone:
   `txfmactl_top` is under-predicted 6.7x by width and has 289 flops and no
   operation over 1,000 bits, so flop count carries something neither captures.

### 31.6 Nothing else is near the cliff

One timeout in 21, and the two nearest designs on the width axis both finished:
`bht_gate` at 614.9 s and `btb_gate` at 328.5 s. Every other CVA6 target is at
most 185,738 bits and 86.7 s. In the 76-target CORE-ET master corpus the
slowest was `vpu_mask` at 242.1 s, and no target there carries a single
operation of 1,000 bits or more. The two CVA6 blocks that would be worst --
`cva6_hpdcache_subsystem` and `cva6_hpdcache_wrapper`, at ~108,000 nodes -- have
no certificate at all, so they never reach this harness.

## 32. LAST PHASE: Lean construction and refactoring research, with `scoreboard_gate` as the benchmark

Where this sits: **after** final acceptance (§6.4), not before and not during.
Every change proposed here moves the numbers acceptance is taken against -- wall
time, peak RSS, which designs reach a verdict inside a bound -- so doing it
first would mean accepting a port against a moving target. The port is accepted
on the code as it stands; this phase then asks how much of the cost was
inherent and how much was how the Lean was written.

### 32.1 Why `scoreboard_gate` is the right benchmark

It is the only target in either corpus that fails to finish, it fails for a
reason that is fully understood (§31), and it comes with three controls in the
same corpus, same toolchain, same harness:

| block | nodes | ops >= 1000 bits | max width | total declared width | wall | verdict |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `scoreboard_gate` | 5,170 | 2,537 | 3,868 | 8,804,743 | >3,603 s | timeout |
| `bht_gate` | 4,832 | 0 | 385 | 791,184 | 614.9 s | agree |
| `btb_gate` | 1,037 | 346 | 2,080 | 741,138 | 328.5 s | agree |
| `decoder_gate` | 3,714 | 0 | 439 | 55,097 | 88.5 s | agree |

`decoder_gate` is the "wide operations absent" control at comparable node
count. `btb_gate` is the "wide operations present, design small" control --
a third the nodes of `decoder_gate` and four times the wall time. `bht_gate`
is the "many narrow operations" control. A refactoring that helps
`scoreboard_gate` and hurts `decoder_gate` has moved cost, not removed it, and
these four together say which.

A benchmark needs a target, not just a baseline. **`scoreboard_gate` reaching a
verdict inside the unchanged 3,600 s bound, with the 32-sample gate and every
checker self-test unchanged, is the goal.** Raising the bound, lowering the
sample count or special-casing the design are all excluded: they change the
measurement instead of the cost.

### 32.2 Three costs that must be separated before anything is optimised

The single biggest mistake available here is to optimise the wrong one. They
are separable, and §31's ladder separates them:

| cost | what it is | `scoreboard_gate` | `decoder_gate` |
| --- | --- | ---: | ---: |
| **(a) literal** | parsing + elaborating the `DesignCert` structure literal | 31.9 s | 66.7 s |
| **(b) model** | elaborating + compiling `reify_design`'s output and `d3_residual` | 238.9 s | 7.1 s |
| **(c) evaluation** | `#eval report` -- 65 `fast` and 33 `denoteResidual` passes | **>= 3,332 s** | ~6 s |

(a) and (b) are elaboration and typechecking. (c) is running the result. For
`scoreboard_gate` the answer is 92 % (c), and **(a) is not the problem at all**:
its literal is 2.5x the bytes of `decoder_gate`'s and elaborates in half the
time. Any proposal that starts "the certificate literal is too big" has to
explain that row first.

### 32.3 What the component measurements say, and what they do not

Raw rows -- probe sha256, whole-process wall, peak RSS, process exit, printed
output, and what each leg actually FORCES -- are retained in
`pass/lean/tests/d3/scoreboard_cost_ladder.tsv`. Everything below that is
called a per-pass cost is a SUBTRACTION against the matching `e1_struct`
baseline, not a measurement, and the baseline moves with machine load. That
distinction decides which of these claims survives.

Measured, whole process (seconds / peak RSS):

| leg | what it forces | `decoder_gate` | `scoreboard_gate` |
| --- | --- | ---: | ---: |
| `e1_struct` | sizes only; no evaluation | 70.6 / 2.67 GB | 290.3 / 5.89 GB |
| `e2_sim1` | ONE `d3_fast` pass | 66.8 / 2.67 GB | 317.9 / 5.90 GB |
| `e5_distinct` | 32 `d3_fast` passes | 71.3 / 2.66 GB | 1,574.7 / 5.89 GB |
| `e3_selftest` | ONE `denoteResidual` + `sameResult r r` | 75.6 / 2.65 GB | 302.3 / 5.90 GB |
| `e4_agree` | 32 `d3_fast` + 32 `denoteResidual` | 173.0 / 2.65 GB | **2,400.1 / 5.89 GB, exit 124** |

**The noise floor comes first.** On `decoder_gate` the four `e*` baselines span
66.8-75.6 s, and in the earlier prefix ladder `s1_cert` (66.7 s) ran SLOWER
than `s2_step` (37.6 s), which is a strict superset of it. So on that design
nothing below about 10 s is resolvable, and the `e2`, `e5` and `e3` marginals
are all inside it. `scoreboard_gate` baselines are steadier (290.3 / 302.3 /
317.9) but a 290 s baseline still carries tens of seconds of wander.

**What survives.**

* 32 `d3_fast` passes on `decoder_gate` cost at most the noise floor:
  `e5 - e1` is 0.7 s, so **under 0.2 s per pass**, against 40.1 s per pass on
  `scoreboard_gate` (`e5 - e1` = 1,284.5 s over 32, far above any wander).
  A factor of at least 250, tracking the wide-operation count, and the single
  most solid number in this table.
* `agree` on `decoder_gate` costs about 102 s marginal, and since its 32 `fast`
  passes are bounded under 5 s in total, essentially all of it is 32
  `denoteResidual` passes -- roughly 3 s each.

**`e4_agree` on `scoreboard_gate` did not complete.** It was killed by its own
`timeout 2400` at 2,400.13 s, exit **124**, with an EMPTY output -- no verdict
was printed. It is undecided in exactly the way the sweep's row is, and it
gives a bound, not a cost: the marginal is `> 2,110 s`, and with `e5`'s 32
`fast` passes known to be 1,284.5 s, **32 `denoteResidual` passes cost more
than 825 s, i.e. more than ~26 s each**.

**What does NOT survive, and I stated it too strongly before.** I wrote that
"the two sides swap places": that `scoreboard_gate`'s interpreter is cheaper
than its compiled model. The `e4` bound refutes it. The interpreter is at least
~26 s per pass against the compiled model's 40.1 s -- the same order, with the
model still ahead, and nothing finer than that is established.

**An anomaly worth carrying into the phase rather than explaining away.**
`e3_selftest` forces one `denoteResidual` and came in at 12.1 s marginal, while
`e4` puts a single interpreter pass above ~26 s. Those do not reconcile. Either
the 12.1 s is baseline wander, or `sameResult r r plan` -- a value compared
against ITSELF -- forces materially less of `r` than `sameResult fast residual`
does, in which case `checkerSelfTest`'s `base` leg is not exercising what its
name suggests. **I do not know which, and the measurement here cannot tell
them apart.** It is a direct question for §32.4-B, because "what does the
interpreter actually force" is the same question as "why is it cheaper than the
compiled model on `decoder_gate`", and both have to be answered before any
optimisation of that path means anything.

### 32.4 The candidates, each with the measurement that settles it

**A. The bitwise primitives.** `bv_bitwise`, `bv_not`, `bits_to_int` and
`bv_bit` in `Translation/LGraphModel.lean` are bit-at-a-time over
`List.range w`, allocating a big-integer modulo, a shift, a power of two and a
list cell per bit: 550 us at 64 bits, 13,550 us at 3,868 (§31.4). `Nat.xor`,
`Nat.land` and `Nat.lor` are native and O(w/64). The repair is a PROVED
equality -- `Nat.testBit` lemmas -- and then the swap. It is NOT an
`@[implemented_by]`: that would put an unverified implementation on both sides
of the D3 comparison, where a wrong one makes the two sides agree wrongly, and
the entire point of this harness is that the two sides are independent.
*Settles it*: the micro-benchmark at 64/256/1024/2080/3868 bits, then
`scoreboard_gate` end to end.

**B. The residual interpreter.** `denoteResidual` does
`runBindings R.bindings.toList (sourceEnvArr R.sources i s)`, and `runBindings`
is `runBindings bs (env.push (denoteExpr env b.rhs))` over `SlotEnv = Array
CertVal`. Reads are O(1) (`denoteRef` is `env[r]?.getD badVal`), so the
suspects are the per-pass `Array.toList` of 5,170 bindings and whether
`env.push` is destructive or copying -- `denoteExpr env b.rhs` borrows `env` in
the same expression that consumes it, and if the reference count is not 1 at
the push then every binding copies the whole environment and the pass is
O(n^2). *Settles it*: count the copies. Lean can report this directly, and a
pre-sized `Array.emptyWithCapacity` plus a binding loop over the Array
(no `toList`) is the alternative to measure against. Note §32.3 says this path
costs ~3 s per pass on a design where the compiled model costs nothing, so
there is real headroom here independently of wide bitvectors.

**C. The certificate literal, if (a) ever becomes the bottleneck.** 10,894
structure literals inside `#[...]`. The alternative already exists and is
already proved: DCERT2 plus `Compiler.CertIO.parseCert_renderCert`, which is
total and invertible for every `DesignCert`. A probe could carry ONE
`ByteArray` literal and decode it, instead of 10,894 elaborated structures.
*Settles it*: the (a) column above on a design where (a) dominates. On
`scoreboard_gate` it would buy 31.9 s of 3,603 and is not worth doing first.

**D. The shape of the emitted model.** `reify_design` emits one straight-line
`let` chain; (b) is 238.9 s of `scoreboard_gate`'s pre-evaluation cost. Three
alternative reifiers already exist and the sweep can select them --
`--reifier named`, `shared`, `chunked` -- and `--proof-segment-size` splits the
walk further. They were built for the proof stage; nobody has measured them on
ELABORATION cost. *Settles it*: the (b) column for all four reifiers on
`scoreboard_gate` and the three controls.

**E. The scheduler's cost model.** Not a Lean refactor, but it belongs to the
same finding, and the measurement is already done: `pass/lean/tests/d3/
d3_cost_census.tsv` carries all 97 measured targets with both what the sweep
recorded and what the certificate contains. Node count fits
`wall ~ nodes^0.45` at R^2 = 0.612 and under-predicts `btb_gate` by 14.9x;
total declared width fits `wall ~ width^0.41` at R^2 = 0.705 and its worst
under-prediction is 6.7x. Neither is sufficient: `txfmactl_top` is the 6.7x
case and has 289 flops with no operation over 1,000 bits.
*Settles it*: a composite predictor fitted on the census and validated by the
only question a bound has to answer -- would it have given `scoreboard_gate`
more than 3,600 s and every other target less.

### 32.5 Rules this phase inherits

The gates do not move to make the numbers work. No sample count is lowered, no
timeout is raised as a repair, no design is special-cased, no checker self-test
is dropped, and nothing becomes `native_decide` that was not. A change to a
semantic primitive carries its proofs with it or it does not land. And every
result is stated against the four-design benchmark, not against
`scoreboard_gate` alone -- the failure mode of this kind of work is a change
that moves cost from the design being watched onto the ones that are not.

### 32.6 What would make this worth publishing

The useful artefact is not "we made one design faster". It is a measured
account of which of (a), (b) and (c) a verified-compiler certificate pipeline
actually pays, how each scales -- in the design's node count, binding count and
TOTAL BIT WIDTH -- and which Lean-level constructions move which. The data for
that already exists in this branch, as `pass/lean/tests/d3/d3_cost_census.tsv`:
97 targets with measured wall time and peak RSS beside node, source, binding,
flop, async-flop and width-distribution counts, built by
`pass/lean/scripts/d3_cost_census.py` from the sweep tables and the
certificates themselves.

## 33. The proof stage: two defects, found on the first eleven designs

`run_d3_corpus.sh coreet-master --prove` over the corpus this worktree emitted.
The executable gate is unaffected -- every row still reaches `agree` -- but the
last-mile theorem closes on **one of the first eleven designs**. The failures
are not one defect, they are two, and they need different repairs.

| design | bindings | flops | async | proof | why |
| --- | ---: | ---: | ---: | --- | --- |
| `debug_breakpoint` | 81 | 0 | 0 | **1** | -- |
| `intpipe_csr_file_fl_barrier` | 28 | 5 | 2 | 0 | **unsolved goal on `flopNextV`** |
| `intpipe_imm` | 108 | 0 | 0 | 0 | `simp` maxSteps |
| `intpipe_csr_pmu_read_interface` | 110 | 6 | 6 | 0 | `simp` maxSteps |
| `intpipe_csr_file_conv` | 154 | 7 | 3 | 0 | `simp` maxSteps |
| `intpipe_inst_bits_stage` | 163 | 32 | 0 | 0 | `simp` maxSteps |
| `intpipe_fp_scoreboard` | 200 | 0 | 0 | 0 | `simp` maxSteps |
| `intpipe_int_scoreboard` | 203 | 0 | 0 | 0 | `simp` maxSteps |
| `minion_dcache_atomic_alu_unit` | 315 | 0 | 0 | 0 | `simp` maxSteps |
| `intpipe_mask_scoreboard` | 394 | 0 | 0 | 0 | `simp` maxSteps |
| `intpipe_csr_replay` | 503 | 6 | 6 | 0 | `simp` maxSteps |

Error census over the eleven proof logs: nine `` `simp` failed: maximum number
of steps exceeded ``, one `unsolved goals`, and ten axiom-audit refusals
downstream of those.

### 33.1 A search budget that was never raised

The probe sets `maxRecDepth 1000000` and `maxHeartbeats 0`, and **nothing sets
simp's `maxSteps`**, which is `defaultMaxSteps := 100000` at Lean 4.31
`Init/MetaTypes.lean:208`. The word appears nowhere in `ReifyProof.lean` or in
`d3_sweep.py`.

**I quoted the wrong tactic here and must correct it.** The tactic I printed
is the CHUNKED backend's, from around `ReifyProof.lean:750`. The legacy
`prove_reified` that these probes actually run closes at
`ReifyProof.lean:299-301` with

```lean
simp [$f:ident, $d:ident, Compiler.runBindings, Compiler.denoteExpr,
      Compiler.refBV, Compiler.refBVs, Compiler.refMem, Compiler.denoteRef,
      Compiler.sourceEnvArr, Compiler.compileOutput, CertVal.asBV]
```

-- no `compileFlop`, no `compileMemory`, no `flopNext_eq`, no `CertVal.asMem`.
That difference is not cosmetic; §33.2 below turns on it.

Nine of the eleven failures are this budget, and the two that are not are the
28-binding async case and the 81-binding design that passes. **What eleven
designs do NOT establish is a universal boundary.** The smallest observed
failure of this kind is 108 bindings (`intpipe_imm`, with no flops and no
memories, so it is a pure size effect there) and the largest observed success
is 81. The real boundary lies somewhere between, depends on what the bindings
contain, and the 64 deferred designs have not been asked.

Raising it is a budget change of exactly the kind `maxHeartbeats 0` already is,
and it weakens nothing: the axiom audit is what makes `proof=1` mean something
and it is untouched. The repair is a config on the emitted `simp`, since
`maxSteps` is a `Simp.Config` field rather than a `set_option`. **Not applied
at this checkpoint** -- the run in progress is the "before" measurement.

### 33.2 The legacy transport has no flop or memory rewrites at all

`intpipe_csr_file_fl_barrier` is the exception that is NOT a budget problem: 28
bindings, well inside the default, failing with `unsolved goals` rather than a
step limit. The goal left open is headed by `flopNextV`:

```
⊢ flopNextV 13
    (bv_resize 13 (Residual.rsextV 13 (Residual.rmuxV 13 ...
       (match sourceValue i st (SourceDesc.flopQAsync 4 2 4 0 true) with ...
```

**My first reading of this was wrong twice over**, and both errors came from
quoting the chunked backend's tactic in §33.1.

* I said `flopNext_eq` is in the simp set and still fails to close. It is
  **not** in the legacy set. Neither are `compileFlop`, `compileMemory` or
  `CertVal.asMem`. The legacy transport carries the rewrites for the
  COMBINATIONAL path -- `compileOutput`, `CertVal.asBV` -- and nothing for
  state. A goal headed by `flopNextV` is left open because there is no rewrite
  offered for it, which is a much more ordinary explanation than a rewrite that
  mysteriously does not fire.
* I called it an async-reset defect. **An async source appearing inside the
  open goal does not establish an async cause** -- it establishes that this
  design happens to have one. The gap is in the stateful path generally, and a
  SYNCHRONOUS RAM fixture exposes the same thing, which rules the async-only
  reading out directly.

So the heading this section originally carried was wrong. What
`intpipe_csr_file_fl_barrier` is, is the smallest FRESH design in the corpus
that exercises the stateful transport at all: 28 bindings, 5 flops. That still
makes it a good fixture, for a different reason than I gave.

### 33.3 The run did not reach all 76: it hit the memory budget at target 12

It stopped on the aggregate limit, not on a verdict. `intpipe_alu`'s proof
probe reached **18,079,524 kB (17.24 GiB) on its own**, the sampled hard limit
terminated it, and the 64 targets behind it were deferred. Final state: 12
judged, 64 deferred, `join_rc=1`, 122-row denominator, 46 without a
certificate. Every deferred row is resumable and none is a design failure.

The twelve that were judged, by binding count:

| module | bindings | proof | sim RSS | proof RSS | wall |
| --- | ---: | ---: | ---: | ---: | ---: |
| `intpipe_csr_file_fl_barrier` | 28 | 0 | 1.45 GiB | 6.62 GiB | 42.4 s |
| `debug_breakpoint` | 81 | **1** | 1.46 | 7.59 | 50.0 |
| `intpipe_imm` | 108 | 0 | 1.46 | 6.91 | 69.3 |
| `intpipe_csr_pmu_read_interface` | 110 | 0 | 1.46 | 6.87 | 46.5 |
| `intpipe_csr_file_conv` | 154 | 0 | 1.48 | 7.23 | 46.1 |
| `intpipe_inst_bits_stage` | 163 | 0 | 1.49 | 7.22 | 68.0 |
| `intpipe_fp_scoreboard` | 200 | 0 | 1.47 | 7.45 | 75.8 |
| `intpipe_int_scoreboard` | 203 | 0 | 1.47 | 7.63 | 77.3 |
| `minion_dcache_atomic_alu_unit` | 315 | 0 | 1.51 | 8.54 | 89.6 |
| `intpipe_mask_scoreboard` | 394 | 0 | 1.52 | 9.58 | 108.7 |
| `intpipe_csr_replay` | 503 | 0 | 1.56 | 11.13 | 165.8 |
| `intpipe_alu` | 2,133 | 0 | 2.02 | **17.24** | 279.1 |

Two things fall out of that column. **Mathlib is a ~6.6 GiB floor** on every
proof probe -- the 28-binding design pays it in full -- so at a 20 GB budget
the proof stage can never hold more than two probes, and `--jobs 1` is not a
preference there but the only option. And above the floor the cost grows with
the design: ~10.6 GiB of growth across 2,105 bindings, about 5 MB each.

**A caution about reading that as a diagnosis.** It is tempting to say the
memory is simp thrashing against its step limit, and the data does not support
it: the one design that PASSES, at 81 bindings, used 7.59 GiB -- MORE than the
108-binding design that failed on `maxSteps` at 6.91 GiB. Whether the §33.1
repair lowers this, raises it, or leaves it alone is unknown and has to be
measured, not assumed. It is possible that closing the proof costs more term,
not less.

### 33.4 Status

`proof` is a real gate and it is now reporting a real result: **not `na`, and
not passing** -- 1 of 12 judged, with 64 undecided behind a resource stop. The
76 runtime agreements stand exactly as before and are unaffected; `agree` and
`proof` are separate columns for this reason.

One incidental confirmation: this is the first real-world exercise of the
deferral-reason repair. The 64 rows read *"the run stopped launching new
targets; a previous target was terminated by the HARD aggregate RSS limit at
18079524 kB"* -- the cause and the measured figure, where before they would
have said "aggregate RSS cap reached before launch (0 kB)" about a cap that was
never armed.

Neither proof repair is attempted here. §33.1 is a one-line budget change with
a known shape; §33.2 is a missing rewrite with a 28-binding reproducer. Both
need the baseline above to be judged against, and §33.3 says plainly which part
of that baseline is a measurement and which is a resource stop.

## 34. The join was asserting the opposite of its own input

`debug_breakpoint`'s sweep row carries `proof=1`, and its probe log carries
`axioms=[propext, Classical.choice, Quot.sound]`. The joined row said `na`,
under a header reading *"proof=na throughout: no per-design translation proof
is generated or checked by this pass"*, and the summary printed
`proof 0/122 (na -- not attempted)`.

The cause is three lines of hardcoding. The row constructor set
`"proof": "na"` and nothing ever wrote it again; the header comment and the
summary were literals. So the one table a milestone is read from contradicted
the run it was built from, and it did so in the direction that loses evidence.

### 34.1 Dispositions, independent of `agree`

`proof` is now carried from the result row and classified:

| `proof_status` | means |
| --- | --- |
| `accepted` | the theorem closed AND the audited axiom set is recorded |
| `accepted_unaudited` | it closed, nobody wrote down what it depends on |
| `refused` | the proof stage ran and the theorem did not close |
| `resource` | the stage was STOPPED. Undecided, never a refusal |
| `unattempted` | no proof stage ran for this row |
| `no_certificate` / `not_run` | nothing to prove / nothing ran |
| `uncreditable` | the row is not tied to the bytes the manifest names |

Independence is the point and the controls check it both ways: a design can
agree on 32 samples and have its theorem refused, and a row that claims
`proof=1` can be uncreditable because its certificate hash does not match.
That last case leaves `proof` **empty**, not `na` and not `0` -- "na" reads as
not attempted and "0" as refused, and a proof that was attempted and cannot be
credited is neither.

### 34.2 `accepted` now requires the axioms, because they were being discarded

`PROOF_GATE_RE` already captured the audited set and the code threw it away:
`thm, _axioms = hits[0]`. A row saying `proof=1` that cannot say what it
depends on is an assertion. The sweep records `proof_axioms` now, and a row
without it reports as `accepted_unaudited` and is NOT counted as accepted.
Re-joining the existing table shows exactly that -- 1 `accepted_unaudited`, 10
`refused`, 65 `resource`, 46 `no_certificate` -- because that run predates the
column. It is a demonstration, not the new evidence.

### 34.3 A search budget is not a refusal

`simp` reporting "maximum number of steps exceeded" leaves `sorryAx` behind and
exits nonzero, which from the exit code alone is indistinguishable from a
theorem that is false. They are opposite claims: one says the search stopped,
the other says it finished and failed. The sweep now records `proof_stop` as a
FIELD, and the join maps any non-empty `proof_stop` to `resource`. The nine
first-pass failures in §33 were budget stops, so they are **undecided**, not
refusals, and the ledger's §33 table should be read that way.

### 34.4 The corpus-origin label, derived

The header asserted "IMPORTED CERTIFICATES: compatibility evidence only" on
every join, including those over a corpus this worktree emitted. It now reads
the sidecar's `certs` paths -- which sit at the TOP of the document, not under
`config`, a level I had to correct after the first version answered UNKNOWN for
everything -- and says EMITTED BY THIS WORKTREE, IMPORTED, MIXED, or UNKNOWN
when there is no sidecar to ask.

21 control legs on the real CLI in `pass/lean/tests/d3/d3_join_proof_controls.py`:
every disposition, the axiom set surviving verbatim, a budget stop separated
from a refusal, a mismatched row crediting neither gate nor proof, the four
origin answers, and refusals for a mismatched manifest digest, a tampered
results table and an unbound sidecar. Denominators preserved.

## 35. The proof-only transport repair, applied to source

The prepared candidate (`audits/d3_legacy_stateful_transport_isolated_20261010/
candidate_proof_only_patch.diff`) is **30 added lines, all proof**:

* `bv_resize_idempotent` -- resizing twice to the same width is the first
  resize;
* `flopNextV_resize_din` -- the legacy emitter resizes `din` before calling the
  clock-aware helper, so the resize can be dropped inside it;
* and the legacy closing `simp` gains `compileFlop`, `compileMemory`,
  `flopNext_eq`, `flopNextV_resize_din`, `CertVal.asMem`.

It does not touch the emitter, the theorem statements, the axiom gate or any
contract. The statements stay universally quantified over edges, inputs and
state.

Its evidence packet is specific about what it does and does not show. The
isolated fixture proves with standard axioms (`bridge_and_raw_din`, exit 0);
the unpatched baseline fails (exit 1) with a goal containing `rmemWriteV` and
`memImg` -- a MEMORY, which is what rules out my "async-reset" reading in §33.2
directly; the bridge lemma ALONE still fails on a resized `din` (exit 1), so
`flopNextV_resize_din` is load-bearing rather than decorative; and a deliberately
wrong flop model is refused (exit 1), so the repair is not proving everything
put in front of it. The production CLI packet records
`candidate_reifier_sha256 == master_reifier_sha256`, i.e. that run used the
UNPATCHED reifier, and **the fresh 28-binding barrier design timed out at 120 s
and is NOT proved by that evidence.**

Added beside it: an **explicit finite** `simp (maxSteps := 4000000)` on that
same tactic. Finite on purpose -- an unbounded search turns a design that will
never close into a job that never ends, and the sweep would have to separate
those by wall clock. A budget that is exceeded is undecided (§34.3), so raising
it later costs time and retracts no result.

**Status: edited, not elaborated.** Rebuilding the proof library touches
`.lake/build`, which the running unbounded `scoreboard_gate` measurement reads,
and that measurement is the current job. The build, the bounded RAM
positive/wrong-flop controls, the real barrier design, `intpipe_imm` and the
proof procedure all wait for it. Fresh output and fresh fingerprints when they
run: the existing `--prove` table predates this change and is not current
evidence for it.

## 36. I destroyed my own measurement, and the guard that stops it happening again

The unbounded `scoreboard_gate` run died at **10:38.85 wall, 5.62 GiB peak,
exit 135 -- SIGBUS**, with empty output. Not an OOM: the cap was 18,000,000 kB
and the cgroup peak was 5.16 GiB. Not a property of the design either.

`d3_runner_test.py` mutates the LIVE build. One leg renames
`formal/lean/.lake` aside and symlinks it; another appends a byte to the live
`D3Harness.olean` and writes it back. Both restore what they changed and both
are harmless on an idle machine. They are not harmless beside a running probe:
`lean` mmaps the `.olean`s it imports, so rewriting one under it invalidates
pages inside a live mapping and the process dies of SIGBUS -- with no
diagnostic pointing anywhere near the cause.

I ran that test, to chase a flake, while the measurement was running. I had
been told explicitly not to run competing builds against its imports. The
measurement was lost and had to start again from zero.

### 36.1 The guard

`refuse_if_build_in_use` scans `/proc/*/maps` for any other process of ours
holding the build root, and both mutating legs call it first. It prints the
pid and the command line and exits 2 rather than proceeding. Verified against
the live measurement:

```
REFUSING to mutate the live build: another process has it mapped.
  pid 1317609: .../bin/lean .../scoreboard_gate_s5_full.lean
Rewriting an .olean under a running `lean` invalidates pages inside its
mapping and kills it with SIGBUS. Wait for those to finish.
```

A guard that turns silent destruction of someone else's run into a visible
refusal is worth more than the two legs it occasionally blocks. This is the
second time in this port that concurrent access to one Lake context produced a
failure with a misleading signature -- the first was 7,090 Mathlib
`.olean.server` replay errors from running tests beside a module proof -- and
neither was detectable from the symptom.

### 36.2 Carried forward, not chased

`resume_after_kill` in `d3_runner_test.py` failed **1 run in 3** under load,
with *"resume completed the run to 2 rows"* -- the resume adding none of the
four remaining targets. That is a statement about resumability, not about the
assertion, and it deserves its own investigation. It is NOT being chased now,
because chasing it means running the test that destroys the measurement, and
the measurement is the current job. `timeout_resumable` in the scheduler test
failed once in the same window and is on the same list.

## 37. D3 handoff checkpoint

Stopping here at the user's direction to start the Futamura projection rebase.
**Reordering closes nothing.** D3 translation validation, D4
incremental/differential and FP all remain open, and the plan order is now:

> this D3 checkpoint -> **FP rebase** -> remaining D3/D4 procedures and final
> integrated validation -> ten-block byte checks per original branch ->
> scoreboard timing and the Lean-construction research (SS31-32) last.

### 37.1 The scoreboard attempt was stopped, not finished

Identity verified before anything was signalled: lean PID 1317609,
`starttime=203801332`, uid 1008, parent 1317608 (`/usr/bin/time`,
`starttime=203801324`), probe `scoreboard_gate_s5_full.lean` sha256
`0ada49ea...61963`. All four matched.

Terminal evidence, captured BEFORE the stop: elapsed 1:05:29, RSS 5,814,868 kB,
cgroup `memory.current` 5,217,636,352 B, `memory.peak` 5,229,641,728 B,
`memory.max` 18,432,000,000 B, `memory.events` **oom 0, oom_kill 0**, and
exactly the two expected pids in `cgroup.procs`.

Then SIGTERM through its own guarded runner so `/usr/bin/time` still wrote its
report: **exit 143, "Command terminated by signal 15", 1:05:39 wall, max RSS
5,900,964 kB, 0 bytes of output.**

**Disposition: OPERATOR-STOPPED / DEFERRED at the user's request.** Not a
timeout, not an OOM, not a semantic failure, not a completed measurement, and
not a proof. The probe is a runtime/mutation report, not a translation-proof
command.

What it did establish: at 3,939 s it was **9% past the 3,600 s bound the sweep
used**, still running, with no output and no memory pressure of any kind. So
`scoreboard_gate`'s real completion time is `> 3,939 s` and remains unmeasured.
Attempt 1 was unenforced and terminated; attempt 2 died of SIGBUS because I ran
a mutating test beside it (S36); attempt 3 is this one. All three are recorded
in `diag/UNBOUNDED_README.txt`. Afterwards: no `lean` process alive, and **zero
processes mapping the shared build**, so builds and mutating tests are safe
again.

### 37.2 What is built and validated, and what is only edited

| | state |
| --- | --- |
| CORE-ET master corpus, executable gate | 76/76 `agree`, join complete, `join_rc=0` |
| CVA6 frozen-30 master corpus, executable gate | 20/30 `agree`, 1 timeout, 9 no certificate |
| CORE-ET imported corpus | 77/85 judged, all `typecheck=0`; 1 `rss_killed`, 7 deferred |
| CORE-ET master `--prove` | 12/76 judged, **64 deferred on memory**; 1 accepted (unaudited), 10 budget stops |
| transport repair + `simp (maxSteps := 4000000)` | **edited in source, NOT elaborated** |
| join/sweep proof dispositions | built, 21 control legs pass |
| `d3_cost_census.tsv`, `scoreboard_cost_ladder.tsv` | retained evidence |

Four distinctions that must survive this handoff: **runtime `agree` is not a
translation proof** (`proof` is a separate column and is `na` on every executable
row); **a resource stop is not a semantic failure** (64 deferred, 1 rss_killed,
1 timeout, 9 budget stops -- none is a design result); **deferred is not
covered** (the `--prove` pass reached 12 of 76); and **edited is not built**
(SS35 changes no result until the library is rebuilt, which will need fresh
output and fresh fingerprints).

### 37.3 Open, carried forward

From `audits/d3_join_open_accounting_20261011/result.json`, against my own join
repair at this HEAD -- all five are real and none is fixed here:

1. An **unauthenticated** join (no `--meta`, no `results_sha256`, no
   `manifest_digest`) exits 1 but still emits `proof_status=accepted` and a
   "1/1 accepted" summary. An observed claim is not authenticated milestone
   credit and the output does not yet separate them.
2. `proof_axioms` is admitted as any non-empty string -- `[sorryAx]` or
   garbage. No permitted-set parsing. A valid empty `[]` must stay legal.
3. An unrecognised `run_status` (`runner_error`) was credited `proof=1`,
   `agree=1` at exit 0. Credit should require a recognised finished-eligible
   state.
4. `corpus_origin` treats ANY path inside the worktree as "emitted here",
   including a non-existent imported-copy directory. Path containment is not
   emission provenance; the fresh corpora have `PROVENANCE.tsv` source hashes
   and D4 `EMITTED` rows that are the actual evidence.
5. Legacy rows predating `proof_stop` -- rc 124 and kernel OOM -- are still
   mislabelled `refused`. They need evidence or an explicit unknown, not a
   guess.

Plus, from the same packet: the join does not carry the module backend's
separate `module_proof` admission at all. A module worker can earn
`module_proof=1` while the in-process `proof` stays `na`, and that is not an
unattempted module stage.

Also open: `resume_after_kill` (1 failure in 3 under load, the resume adding
none of the four remaining targets) and `timeout_resumable` (1 failure),
neither chased because chasing them meant running the test that destroys a live
measurement.

## 38. FP checkpoint 1: installed, adapted, built, and two real reconciliation gaps

Source: read-only `/WORKROOT/projects/livehd-futamura/livehd` at
`9bb41a09d6b57111ed2b3fd10102ec4ff174b6c6`, semantic pin
`90583be1349acbd25f740b176a406aa8bbe1251c`. Asset list and pinned digests from
`audits/fp_original_procedure_baseline_20261010/result.json` -- named in
advance, not discovered by walking the tree.

### 38.1 Counts

| | n | |
| --- | ---: | --- |
| pinned assets installed | **56** | 38 Projection modules + 18 procedure entrypoints |
| of those, verbatim | **53** | installed digest = pinned digest |
| adapted | **3** | 6 edits, each forced and recorded |
| support assets installed | **31** | docs, `certio/` experiment record + perf artifacts, census, overlays |
| shared-core modules built | **8** | |
| Projection modules built | **16** | of 25 non-Proto |
| blocked by a real failure | **1** | `OperatorBridge` |
| blocked behind it | **8** | cascade only |
| unattempted | **13** | the `Proto/` modules; not in the original driver's list |
| original fixtures executed | **3** | below |

`pass/lean/tests/fp/fp_install_map.tsv` is the source-to-master preservation
map: one row per asset with the pinned digest, the digest actually installed,
`verbatim`, the edit count and the reason. An unadapted row has equal digests,
so "verbatim" is checkable rather than claimed.
`fp_support_assets.tsv` records the support assets AS OBSERVED -- they are not
in the pinned baseline, so their digests are not verified against a prior pin.

### 38.2 The three adaptations

1. **`Projection/CertLoad.lean` -> the V1 route.** FP's `Compiler.CertIO` is
   the DCERT1 codec; master's is DCERT2 and FP's lives beside it as
   `Compiler.CertIO.V1` (S22). Left alone, CertLoad would silently take the
   DCERT2 contract and its own grammar guard --
   `#guard (parseCert "DCERT1\n0\n0\n0\n0\n0\n".toUTF8).toOption.isSome` --
   would fail on magic. The V1 route keeps the HISTORICAL guards testing the
   grammar they were written for. **The fresh DCERT2 transport is a separate
   obligation and is NOT discharged by this.**
2. **`.build-proj.sh`.** The driver is itself a pinned asset, so its adaptation
   has to live in the installer or a re-install reverts it -- which happened
   once, silently taking `Compiler/CertIOV1` out of the list and breaking the
   whole CertLoad chain. Master's `Compiler/CertIO` is also dropped from the
   list: DCERT2 imports `CompileDesign`, whose chain imports Mathlib, and this
   driver is deliberately "no lake, no Mathlib" (it excludes
   `Translation/OpBridge` for the same reason). That is out of scope, not a
   failure.
3. **`OperatorBridge.lean`, three `rfl` theorems.** See S38.4.

### 38.3 The overlay, adapted rather than applied

FP carries one overlay on the shared semantics,
`Projection/overlays/bv_shl_step.patch`: it factors the `Op_SHL` fold term out
as `bv_shl_step` so three Projection modules can name it, and rewrites two call
sites.

**It must not be applied verbatim.** Its body is the UNGUARDED `2 ^ k` formula,
and master's `Op_SHL` saturates at the result width -- the comment on master's
`eval_op` says in as many words that the bound is the bridge's and is NOT
claimed equal to the unguarded formula on every input. Taking FP's body would
delete a boundary repair this branch proved, which is the R46 regression again.

So master gets the name with MASTER'S body, guard included, and **no call site
is touched** -- `eval_op` is byte-identical to before, which makes the overlay
inert in the strongest sense. Without the definition,
`ObjectLanguageSemantics` does not elaborate at all (`Unknown identifier
bv_shl_step`), and with it, 16 Projection modules build.

### 38.4 Two reconciliation gaps, both real

> **CORRECTED IN S39.** Both "gaps" were failures of DEFINITIONAL REDUCTION,
> not false statements. Every one of FP's four original claims is TRUE on
> master and is now proved, with its statement unchanged. The reading below --
> that the claims are false here -- is wrong and is kept only so the correction
> has something to point at.


Both are FP claims written against the PINNED BASE that are false against
master's boundary repairs. Neither is a defect in master, and neither is
repaired by weakening master.

**(a) `Op_SHL`, resolved.** `OperatorBridge` carries three `rfl` theorems named
`..._unchanged_by_overlay`, whose job is to certify the overlay changed
nothing. They assert `eval_op`/`denote_op` equal a fold over the unguarded
formula. On master those are **false**, and the build named all three. Since
the overlay here is inert, the statement that says so names master's step
instead; restated, the three hold by `rfl`.

**(b) `Op_Sext`, OPEN.** With (a) fixed the build advances to
`OperatorBridge.lean:333`, `evalOp_Sext`, whose closing `split <;> rfl` fails:

```
left:  { width := w, value := a.value % 2 ^ a.width % 2 ^ w }
right: { width := w, value := (if ... < 2 ^ (k - 1) then ... else ... - 2 ^ k) % 2 ^ w }
```

That is master's **strict-greater Sext boundary** -- the third of the three
boundary repairs S6.1 requires be preserved. FP's bridge was written before it.
This needs the theorem restated against master's `Op_Sext`, which is proof work
with a real obligation attached, not a mechanical edit. **Not attempted at this
checkpoint.** `HardwareInterpreter`, `HardwareAdequacy`, `ProjectionCorrect`,
`ResidualFragment`, `ProjectedStep`, `CertLoad`, `Scaling` and `Audit` are
behind it and are blocked, not failing.

### 38.5 Original fixtures executed on master

| fixture | exit | wall | peak RSS | guards |
| --- | ---: | ---: | ---: | ---: |
| `Projection/Demo.lean` (first projection) | **0** | 1.40 s | 487,180 kB | 28 |
| `Projection/Gate0.lean` (self-application / second projection) | **0** | 3.01 s | 493,632 kB | 16 |
| `scripts/repr_prototype.lean` (diagnostic runner) | **0** | -- | -- | -- |

`Gate0`'s own header says *"Everything here is a `#guard`, so the build is the
test"*, and it asks two questions in order: does `mixProgram` compute what the
Lean `mix` computes, and does specializing it to the toy interpreter produce a
compiler. Both execute on master, with the shared core built from master's own
post-D2 sources.

**What this does not close.** Three fixtures are not the inventory. The
check-off standard is every benchmark block that passed on the original branch,
under each original applicable procedure, joined to fresh master execution with
hashes, commands, exits and observable results -- and the 311 primary axiom
reports cover only 21/38 modules. The `Proto/` modules, ProtoAudit, the csimp
obligations, the runner/trace/scaling/DCERT experiments (the 30-row
`certio/experiments.jsonl`), the historical V1 replay and the fresh DCERT2
transport are all retained and all open. Object second projection stays
distinct from unproved host-specializer correctness.

## 39. FP checkpoint 2: the bridge was provable all along

### 39.1 The analysis S38 got wrong

I wrote that FP's `Op_SHL` and `Op_Sext` bridge claims are "false on master",
on the evidence that their `rfl` proofs fail. **That inference is invalid.** A
`rfl` failure says the two sides do not REDUCE to one another; it says nothing
about whether they are equal. Master's `eval_op` carries two boundary repairs
the pinned base did not have, so of course it does not reduce to the base's
shape -- and both equalities hold anyway.

All four original statements are preserved verbatim and **proved**:

| theorem | axioms |
| --- | --- |
| `Projection.evalOp_Sext` | `[propext, Quot.sound]` |
| `Projection.bv_shl_step_unfolds` | `[propext, Quot.sound]` |
| `Projection.evalOp_SHL_unchanged_by_overlay` | `[propext, Quot.sound]` |
| `Projection.denoteOp_SHL_unchanged_by_overlay` | `[propext, Quot.sound]` |

No `sorryAx`, no axiom added, no gate weakened, nothing excluded from support,
and `>` was not relaxed to `≥`. The adaptation is **proof-only**: three `rfl`s
and one `split <;> rfl` became real proofs. Statements, support coverage and
the universally quantified contracts are untouched.

### 39.2 The two arguments

**`Op_SHL`.** Master guards the fold term with `if k ≥ w then 0`. That is the
same value: `2 ^ k = 2 ^ w * 2 ^ (k - w)` when `k ≥ w`, so `2 ^ w` divides
`u * 2 ^ k` and the product is `0 mod 2 ^ w` -- which is what the guard returns.
`shl_step_eq` proves the step, `shl_fold_eq` lifts it over the fold by
`funext`, and the two original assertions follow by `unfold` + `exact`. The
guard exists so the term EVALUATES at amounts that are values rather than
widths; it was never a different answer. Master's own comment said the equality
"is NOT claimed" -- true of the evidence then, not now -- and both copies of it
have been corrected to point at the proof.

**`Op_Sext`.** Master branches at `n > a.width`, STRICTLY, and returns plain
zero extension there. `sext_wide` is the arithmetic: `bv_uint a` is non-negative
and below `2 ^ a.width`; `n > a.width` gives `a.width ≤ n - 1` and so
`bv_uint a < 2 ^ (n - 1)`, which both leaves the value unchanged mod `2 ^ n`
and puts the sign bit at `n - 1` clear. The other branch splits the same sign
test on both sides. `sint_resize` and `evalOp_Sext_shape` carry it, and the
original theorem is `unfold eval_op; exact evalOp_Sext_shape w a _`.

Core lemmas only -- `Int.pow_pos`, `Int.emod_nonneg`, `Int.emod_lt_of_pos`,
`Int.emod_eq_of_lt`, `Nat.pow_le_pow_right`, `Int.mul_emod_right` -- because
`.build-proj.sh` builds without Mathlib.

### 39.3 Finite boundary controls, and a demonstration that they discriminate

`pass/lean/tests/fp/fp_sext_controls.lean`, **16** `#guard` directives plus 6
`#print axioms`, exit 0 in 0.69 s:
`n = 0` at two widths, `n < a.width` truncate-then-sign-extend in three signs,
`n = a.width` with the sign bit set, `n > a.width`, zero-width input, zero-width
result, unreduced `BV.value` (`⟨4, 19⟩` is 3), negative `BV.value` (`⟨4, -1⟩` is
15), a negative AMOUNT, and signed/unsigned at a common width.

**The `≥` detector is shown to detect.** A control that would pass against the
wrong implementation is worth nothing, so the same function with `>` weakened to
`≥` was built beside it and evaluated:

| input | master (`>`) | the `≥` variant |
| --- | ---: | ---: |
| `a = mk_bv 4 (-1)`, `n = 4`, `w = 8` | **255** | **15** |
| `a = mk_bv 4 (-1)`, `n = 5`, `w = 8` | 15 | 15 |

The first separates them; the second does not. It is the PAIR that is the
control, and the file says so.

These are additional evidence. The obligation is the universal theorem, which
holds for all `w`, `a` and `amount`.

### 39.3a A counting correction

Earlier guard totals came from `grep -c '#guard'`, which counts LINES MENTIONING
the word -- prose in a doc comment included. Counting DIRECTIVES instead:
`fp_sext_controls` has **16** (not 17), `Gate0` **16** (not 17), `Scaling` **31**
(not 33). `Demo` (28) and `CertLoad` (13) were right. `fp_sext_controls` also
carries 6 `#print axioms`, a different kind of directive, counted separately.

### 39.4 Rebuilt, and the original fixtures re-run

`.build-proj.sh` from a cleared `.olean-dev`: **33 modules, 0 failures,
`build_rc=0`** -- 8 shared-core and all 25 Projection modules, including the
eight that were blocked: `HardwareInterpreter`, `HardwareAdequacy`,
`ProjectionCorrect`, `ResidualFragment`, `ProjectedStep`, `CertLoad`, `Scaling`,
`Audit`.

| fixture | exit | wall | peak RSS | guards |
| --- | ---: | ---: | ---: | ---: |
| `Demo` (first projection) | 0 | 4.97 s | 488,484 kB | 28 |
| `Gate0` (self-application) | 0 | 2.60 s | 496,012 kB | 16 |
| `CertLoad` (historical DCERT1 V1 grammar/admission) | 0 | 1.24 s | 529,144 kB | 13 |
| `Scaling` | 0 | 12.64 s | 535,684 kB | 31 |
| `Audit` | 0 | 0.69 s | 519,556 kB | -- |
| `scripts/repr_prototype.lean` | 0 | -- | -- | -- |

CertLoad's 13 guards are the historical V1 controls, executing and passing on
the V1 route. **The fresh DCERT2 transport remains a separate obligation and is
not touched by this.**

### 39.5 Preservation accounting, repaired

Three defects in my own installer, all found by review rather than by failure:

* **Anchors were validated while writing.** A bad anchor on the last asset
  refused AFTER the first fifty-five had landed. Validation is now a complete
  first pass that writes nothing; only if every anchor resolves does anything
  reach a destination.
* **The map recorded the plan, not the result.** `destination_sha256` is now
  read back from the installed file, and a mismatch between what was written
  and what the file reads back is a refusal.
* **The 31 support assets were "observed", which verified nothing** -- the
  observation and the copy came from the same bytes. They are now bound to the
  PINNED GIT OBJECT via `git rev-parse 9bb41a09d:<path>` and `git cat-file`:
  **29 verified**, 2 `untracked_at_pin` (two perf-artifact logs git does not
  have at that commit), each labelled as the observation it is.

### 39.6 Still open

Everything the check-off standard asks for. The 13 `Proto/` modules are
unattempted; ProtoAudit and the csimp obligations are untouched; the 30-row
`certio/experiments.jsonl` configurations, the eight canonical passing inputs,
the runner/trace/scaling/DCERT experiments and the historical-vs-fresh
transport split are all retained and all open; `pmp_data_if`'s fresh native
emission gap (rc 6, combinational loop) is open, not dropped. The 311 primary
axiom reports still cover 21 of 38 modules. Object second projection stays
distinct from unproved host-specializer correctness. Module builds and fixture
exits close no benchmark coverage.

## 40. FP checkpoint 3: all 38 modules, and the first historical benchmark

### 40.1 The 13 Proto modules, in dependency order

Order derived from actual imports, not assumed: `InterpreterVariant`,
`MixProgramSummary`, `PartialEvaluatorFast`, `PartialEvaluatorShift`,
`PartialEvaluatorSummary` have no Proto dependencies;
`VariantTransport <- InterpreterVariant`, `RewriteTotal <- VariantTransport`,
`VariantAdequacy <- RewriteTotal`, then `VariantExec` and `ProtoAudit` off
`VariantAdequacy`, `RunnerSupport <- VariantExec`, and the two mirror checks
last.

**All 13 build, rc=0, zero errors, and NONE needed adaptation.** The pinned
`.build-proj.sh` builds them unmodified -- `Proto/X` resolves through its
`Projection/` branch -- so the driver stayed verbatim for this.

| module | wall | peak RSS | | module | wall | peak RSS |
| --- | ---: | ---: | --- | --- | ---: | ---: |
| InterpreterVariant | 1.37 s | 510,920 kB | | VariantAdequacy | **25.95 s** | **1,754,676 kB** |
| MixProgramSummary | 1.45 s | 545,964 kB | | VariantExec | 1.29 s | 521,896 kB |
| PartialEvaluatorFast | 6.04 s | 581,128 kB | | ProtoAudit | 0.73 s | 557,136 kB |
| PartialEvaluatorShift | 6.16 s | 559,940 kB | | RunnerSupport | 0.91 s | 531,544 kB |
| PartialEvaluatorSummary | 6.23 s | 590,276 kB | | LookupMirrorCheck | 2.84 s | 515,560 kB |
| VariantTransport | 1.56 s | 610,132 kB | | S2aMirrorCheck | 2.79 s | 515,716 kB |
| RewriteTotal | 1.46 s | 549,096 kB | | | | |

That is **38 of 38** Projection modules built from master sources in one
isolated project-local `.olean-dev` under pinned Lean 4.31. The evidence is the
clean-rebuild command and its exit, not the presence of `.olean`s.

### 40.2 The original assertions execute

| module | exit | wall | peak RSS | `#guard` | axiom reports |
| --- | ---: | ---: | ---: | ---: | ---: |
| `Proto/ProtoAudit` | 0 | 0.68 s | 515,644 kB | -- | **43** |
| `Proto/VariantExec` | 0 | 1.19 s | 505,904 kB | **22** | -- |
| `Proto/LookupMirrorCheck` | 0 | 2.73 s | 503,884 kB | **12** | -- |
| `Proto/S2aMirrorCheck` | 0 | 2.65 s | 503,548 kB | **13** | -- |
| `Audit` (primary) | 0 | 0.68 s | 523,864 kB | -- | **311** |

Axiom sets across all 354 reports: 158 `[propext]`, 118
`[propext, Quot.sound]`, 75 `[propext, Classical.choice, Quot.sound]`, 6 with
none. No `sorryAx`, nothing outside the permitted set, no declaration dropped.

`VariantExec` is the smallest original variant SEQUENTIAL fixture, and it is
the one the checkpoint asks for: explicit budgets (20000/200) rather than
`projectDesign`'s hardcoded pair, reset beating enable, enable-low holding,
output = the OLD `q`, next-state flops per cycle, and a 3-cycle trace where
each cycle starts from the previous `nextState` -- plus a fail-closed case at
an impossible budget.

### 40.3 `proto_probe --rewrite-agree`, the original CLI

```
hwS main: partial-vs-total  same-body true  same-report true
          (applied true/true, beforeTotal true/true)
negS: partial beforeTotal false applied false | total beforeTotal false applied false
  OK: the kernel-visible transform agrees with the checked one
```
exit 0, 3.29 s, 571,520 kB. Both the real body and the ORIGINAL negative
fixture `negS`, with the original observable report fields.

**What it is.** The probe's own comment is the right framing and is preserved:
`RewriteTotal`'s traversals are structurally recursive so the kernel can reduce
them; `InterpreterVariant`'s are `partial` and opaque to it. *Nothing can PROVE
they agree* -- a `partial def` has no equations to reason from. This is
regression evidence that the kernel-visible facts are about the same transform
the gate checks. **Runtime agreement on `hwSVar` does not transport the theorem
about `hwSVarT`**, and the coverage index keeps the two apart.

### 40.4 A transport the runners needed

Five original runners -- `proto_probe`, `s2_probe`, `s2a_probe`, `s3_probe`,
`total_probe` -- import `Compiler.CertIO` and call `CertIO.loadCert`. On master
that name is DCERT2 and its chain imports Mathlib, which `.build-proj.sh`
excludes by design. They take the **V1 route**, exactly as `CertLoad` does:
same historical contract, same reason, recorded in the install map. 8 of 56
assets are now adapted; 48 verbatim.

The installer's new validate-before-write pass earned itself immediately: I
registered 5 `CertIO.loadCert` call sites in `proto_probe` from a truncated
`grep`, and it **refused before writing anything**, naming the real count (9).
Under the old code that refusal would have landed after 55 files.

### 40.5 Coverage index over all 38 modules

`pass/lean/tests/fp/fp_coverage_index.tsv`. Totals: **557 theorems, 352 with an
exact-name axiom report** (63%), 854 definitions of which **16 are `partial`**
and so have no kernel theorem by construction, **6 `@[csimp]`** equivalences and
**0 `@[implemented_by]`**, 306 `#guard` directives and 357 `#print axioms`.
Every one of the 22 modules that declares a theorem has at least one audited;
16 modules declare none and are definition-and-guard only.

The index keeps three distinctions the standard asks for: a `#guard` is a
COMPILED-EVALUATOR check and not a kernel result; a `partial def` has no
equations and cannot carry one; and a `@[csimp]` equivalence is a separate
obligation from the theorem it accelerates. The primary `Audit` imports 14
modules directly and transitively covers 21 of 38 -- it is not a full-module
audit, and `ProtoAudit`'s 43 reports are a second, disjoint set rather than an
extension of it.

### 40.6 The first historical benchmark on master

`instr_scan`, the smallest of the eight canonical CVA6 passing blocks (original
record row 15), through the ORIGINAL V1 runner `cert_runner.lean` at the
original invocation `<file.dcert> 4 1`:

```
cert_sha256 41533d1c315437991614088efbfabc325e3daf1a128c2d62fcc27485c5aa20aa
  bytes 39469  read 1 ms  parse 67 ms
  sources 1235  nodes 1264  outputs 13 flops 0  memories 0  clocks 1
  support: wf true  memFree true  sources true  ops true arities true  flopClocks true
  asyncOK (DCERT1 v1 cannot carry FlopDesc.asyncReset): true
  specialize+check 250389 ms   residual funs 1 size 2335903  bound 3241
  surviving caseT tags [120]   (tagState = 120)
  one cycle AGREES: OUT[13] 1:0 1:0 64:28 1:0 1:0 1:0 1:0 1:0 1:0 64:58 1:0 1:0 1:0 | FLOP[0]
  trace AGREES (4 results)
```
exit 0, 293.95 s wall, 627,336 kB peak. The certificate hash matches the
recorded one exactly, and all six `SupportedByProjection` conjuncts are
reported separately, as the original does.

**Its limits, from the procedure itself.** The runner prints: *"agreement here
is EXECUTION EVIDENCE about this file, not a kernel proof about it; `SimWF` is
still undischarged for a loaded certificate."* And `asyncOK` is true because
**DCERT1 cannot carry `FlopDesc.asyncReset` at all** -- V1 does not restore
omitted clock/reset metadata, it reports that the format has none.

This is one block of eight, under one configuration, on the HISTORICAL replay
route. It begins benchmark preservation and closes nothing.

### 40.7 Still open

The other seven canonical inputs; the 30 experiment rows and their
configurations (26 successes are not the inventory); the runner, trace, scaling
and DCERT experiment sets; `pmp_data_if`, whose fresh emission is
`EMIT_FAILED rc=6` with a native combinational-loop diagnostic and which stays
IN the denominator; the csimp equivalence proofs as checks distinct from
executable use through the original runners; the fresh DCERT2 FP transport,
open until the current parser plus FP admission and runtime procedures actually
execute; and conditional `mixDriver`/`checkResidual` premises, which are not
concrete specialization proofs. Object second projection stays distinct from
unproved host-specializer correctness.

## 41. FP checkpoint 4: the native route, and eight historical rows reproduced

### 41.1 Three counting corrections

* **`@[csimp]`: FOUR active, not six.** The attribute is also discussed in
  prose; counting textual mentions on a body with only BLOCK comments stripped
  caught two `--` notes. Active registrations are declaration-attached:
  `PVal.shift_eq_shiftFast` and `PEnv.shiftBy_eq_shiftByFast` in
  `PartialEvaluator`, and the same two names under `ProtoSum` in
  `Proto/PartialEvaluatorSummary`.
* **`ProtoAudit`: 46 reports, not 43.** I counted `depends on axioms` and
  missed the three `does not depend on any axioms` lines. 46 directives, 46
  output lines. **311 + 46 = 357**, which is what the axiom-set distribution
  already summed to (158 + 118 + 75 + 6 = 357); my "354" was inconsistent with
  my own figures.
* **The partial-def claim was too broad.** I wrote that a `partial def` "has no
  kernel theorem by construction". The accurate statement is that it has no
  kernel UNFOLDING EQUATIONS, so a proof cannot reduce through its body and the
  usual equational reasoning is unavailable. That is a limit on how it can be
  reasoned about, not a proof that no theorem can mention or constrain it.

The coverage index is regenerated with comment-aware counting -- block AND line
comments stripped, and `csimp_active` requiring the attribute to sit on a
`theorem`. Totals: 38 modules, 557 theorems, **352 audited**, 854 defs, 16
`partial`, **4 active csimp**, 0 `implemented_by`, 306 guards, 357 axiom
directives. It is an INVENTORY and is labelled as one; it does not promote
itself to axiom acceptance.

### 41.2 The four csimp equalities, and the two obligations kept apart

`pass/lean/tests/fp/fp_csimp_checks.lean`, exit 0 in 0.87 s:

```
'Projection.PVal.shift_eq_shiftFast'              [propext, Quot.sound]
'Projection.PEnv.shiftBy_eq_shiftByFast'          [propext, Quot.sound]
'Projection.ProtoSum.PVal.shift_eq_shiftFast'     [propext, Quot.sound]
'Projection.ProtoSum.PEnv.shiftBy_eq_shiftByFast' [propext, Quot.sound]
```

The file states the split it rests on: the KERNEL obligation is that each
equality is proved, which the axiom report evidences; the COMPILED obligation
is that `@[csimp]` makes the compiler emit the fast body, which only running
the original runners exercises. **A passing axiom report says nothing about
which body was emitted, and a passing runner says nothing about whether the
equality is proved.**

### 41.3 The frozen experiment matrix, configurations not collapsed

`pass/lean/tests/fp/fp_experiment_matrix.tsv` -- all 30 rows of
`certio/experiments.jsonl`, one row each: 26 old passes, 2 old `exit 3`, 2 with
no recorded exit. Runners: 18 `total-probe`, 11 `file-ab`, 1 `host-var`.

**The thing it would have been easy to get wrong.** Rows 15-22 and 23-30 are
the SAME eight designs under `total-probe` at the SAME fuel -- and they are not
duplicates. Row 15 is stage **S1** (`mixDriver` carrying `PRes.val`, binary
`acc023ee...`, 15.66 s); row 23 is stage **S2** (the `nthD` lookup summary
integrated, binary `1b5531a6...`, 9.65 s). My first pass keyed `master_status`
on the design filename alone and reported 23 rows covered. Keyed on
design + runner + fuel + stage it is **8**. An old `exit 3` row is preserved as
its recorded disposition and is NOT an obligation to make pass.

### 41.4 The native route, reconciled and isolated

`build-opt.sh` is already the isolated procedure the checkpoint asks for: it
REQUIRES an absolute project-local `ROOT`, builds into `$ROOT/olean` and
`$ROOT/nat`, takes `OPT` explicitly (default `-O2`), and says in its header that
it exists so `.olean-dev` and `.native-dev` -- the in-flight experiment
contexts -- are not touched. Both it and `build-native.sh` named
`Compiler/CertIO` in the shared dependency list; both now name
`Compiler/CertIOV1`, the same reconciliation `CertLoad` and the probes take.
Dependency order and the invalidation rule are unchanged and nothing is
deleted.

```
OPT=-O2 ROOT=<worktree>/generated/master_port/fp/nat-total-O2 \
  bash scripts/build-opt.sh scripts/total_probe.lean
```
exit **0**, 202.11 s, 1,811,772 kB. Lean 4.31.0 (68218e87), clang 22.1.4.
Binary `nat/total_probe`, 5,372,328 bytes, sha256 `bb30aec586724436...`.

### 41.5 Rows 15-22 reproduced on master

`total_probe <cert> 200000 2000`, sequentially, each under
`systemd-run --scope MemoryMax=8G` with a 1800 s bound:

| row | block | exit | wall | peak RSS | seeds | trace | control | residual | bound |
| ---: | --- | ---: | ---: | ---: | ---: | --- | --- | --- | --- |
| 15 | instr_scan | 0 | 9.45 s | 11,988 kB | 6/6 | agrees | matches | = | = |
| 16 | commit_stage | 0 | 36.07 s | 11,260 kB | 6/6 | agrees | matches | = | = |
| 17 | compressed_decoder | 0 | 8.99 s | 14,708 kB | 6/6 | agrees | matches | = | = |
| 18 | pmp | 0 | 12.37 s | 15,400 kB | 6/6 | agrees | matches | = | = |
| 19 | **pmp_data_if** | 0 | 35.01 s | 20,420 kB | 6/6 | agrees | matches | = | = |
| 20 | alu_wrapper | 0 | 69.31 s | 23,928 kB | 6/6 | agrees | matches | = | = |
| 21 | alu | 0 | 69.57 s | 23,168 kB | 6/6 | agrees | matches | = | = |
| 22 | decoder | 0 | 130.46 s | 29,556 kB | 6/6 | agrees | matches | = | = |

**8/8 certificate hashes match the recorded ones.** All six width-aware seeds
(0,1,2,3,4,7) match `interpretDesign` on every design; the 4-cycle
independent-state trace agrees on every design; the control interpreter matches
on every design; all six support conjuncts are true on every design. Residual
term counts and checker bounds also equal the recorded values -- reported
separately because matching the old size is NOT an original contract.

`pmp_data_if` passes here on its retained historical certificate. Its fresh
master DCERT2 export is a SEPARATE obligation and is still `EMIT_FAILED rc=6`
with a native combinational-loop diagnostic; that is an export disposition and
is not evidence about historical FP replay. It stays in the denominator.

**What is not reproduced, by construction.** The recorded S1 binary was built
on the old branch; master builds from the pinned S3 source. What is compared is
the CONFIGURATION and the OBSERVABLES, never the binary. And the backend's own
note is preserved: `mixDriver` does not kernel-reduce at nontrivial fuel, so
the concrete `hproj` is not certified -- this is trusted native execution, not
a kernel-certified result and not RTL equivalence.

### 41.6 `record_experiment.py`, at its strong form

`selftest` passes (4 runners), exit 0. One path adapted: its scratch directory
resolved to `projects/.perfwork` -- OUTSIDE this worktree -- because
`formal/lean/../../..` meant something different in the old layout. It now
resolves inside the tree, and the no-fallback-to-`/tmp` refusal is untouched.

The recorder then did its job twice over. A `record` without the right trailer
reported *"exit None, MISSING -- the log carries no exit evidence; status is
UNKNOWN"* rather than inferring success, and the fix was to emit the trailer it
parses, not to relax the check. With a `prelaunch` capture the record carries
`code_identity.describes = "the code that ran"`, binary `bb30aec5...`, exit 0
from the time trailer, 6 stimuli all matching, residual 48439, bound 5711,
support all six -- written to `fp_master_experiments.jsonl`, never into the
frozen original manifest.

### 41.7 Coverage, exactly

**8 of 30 matrix rows replayed.** 18 open (including all 8 S2 rows and the 11
`file-ab` and 1 `host-var` configurations), 2 preserved as old `exit 3`, 2 as
old exit unknown. The 30 rows are not the whole original inventory: the runner,
trace, scaling, DCERT, census and fixture procedures and the
CORE-ET/CVA6/DINO discovery are beyond this file and are queued, not waived.
Fresh DCERT2 FP transport stays open until the current parser plus FP admission
and runtime route actually execute.

## 42. FP checkpoint 5: nine file-ab rows, and an admitter that can refuse

### 42.1 My own join asserted a value it had not derived

`fp_native_replay_join.tsv` printed `exit 0` for all eight rows, and its
producer set that column as a **literal**. The public transcript did confirm
all eight were 0, so no observed result is retracted -- but a table that can
say "exit 0" without looking is not evidence, and the retained `.time` files
did not even carry the status.

`pass/lean/scripts/fp_replay.py` replaces it. Every credited field is derived
from a file the run wrote: `.exit` (the OUTER status, captured as `$?`),
`.signal`, `.log`, `.time` (including the `exit %x` trailer the original
recorder parses -- the WORKER's own view), `.cmd` (the exact full launch
command including the guard), `.guard` (scope and limits), `.capture.json`.
Identity is bound by FULL sha256, never a prefix.

`pass` requires ALL of: outer status 0; the worker trailer agreeing; the log
NEWER than the launch marker; the certificate and binary hashes being the ones
the row names; and every required observable present and true.

**Demonstrated refusing**, not assumed to:

| injected fault | result |
| --- | --- |
| certificate hash not the row's | `refused: certificate is 41533d1c..., the row names 0000...` |
| log older than the launch marker | `refused: the log predates the launch marker: stale output` |
| outer status 3 under a success-looking log | `refused` |
| no `.exit` file at all | `refused: the outer status was never captured` |

That last one is how the EARLIER total_probe batch now reads. Re-admitted from
its retained evidence it is **0/8** -- the observables are all still derivable
from the logs, but the process status is not, so those rows are backfilled as
**`observed_evidence_limited`** with `exit_evidence` citing the public
transcript (task `bbq708esj`) and can never be `pass`. They were not re-run:
re-running solely to manufacture provenance would be inventing it.

### 42.2 Per-mode output contracts, because the runners do not share one

The admitter refused all seven canonical file-ab rows on its first pass, with
`required observable missing: support_all` -- and it was right to. `--file-ab`
does **not** print total_probe's `| ALL true` support summary; it says
`seed N: residual ok` where total_probe says `seed N: ok`; its trace line adds
a `clock-domains` count; and it reports the bound as
`checkResidual: ACCEPTED, proved-sufficient bound N` rather than inside a
`residual N terms, checker bound M` line. Each mode's REAL lines are encoded
now. Neither contract is weaker than the other; `--file-ab` additionally has
`variant not applied` as a forbidden marker, so the variant-changed admission
is checked by its absence beside a zero exit.

### 42.3 Two identity questions, settled before running

* **`rt_alu_gate.dcert` is byte-identical to canonical `alu_gate.dcert`** --
  same sha256 `d731ee22a85e77ba...`, same 220,566 bytes. The alias is a genuine
  content identity, not a substitution.
* **`rt_decoder_gate.dcert` is NOT the canonical decoder.** 301,042 bytes,
  sha256 `f87080df26fcfa7b...`, against the canonical 299,842 bytes and
  `23b4f0316c46af8d...`. A different certificate; it is run as itself.
* **Row 2's original identity is incomplete.** Its `code_identity` says so in
  its own words: *"the WORKING TREE WHEN THIS RECORD WAS WRITTEN, which is NOT
  evidence about what executed; the binary that ran was not hashed and is
  UNKNOWN"*, and it records no command. Master's replay of row 2 is a fresh run
  of that input under this configuration; it does not reproduce an old identity
  that was never captured.

### 42.4 The nine file-ab rows

`proto_probe` built at `-O2` into its own ROOT: exit 0, 201.34 s, binary
`ec40994e40e625ecab040bc561d697c3b2aadc254a3e284ad44bedfe9cbe488f`. The V1
dependency route and the native invalidation rules are unchanged.

Canonical rows 6-12 at 1800 s, then configured rows 2-3 at the recorded finite
3600 s cap (the old decoder run took ~2900 s). Sequential, each under
`systemd-run --scope MemoryMax=8G`.

**9/9 admitted**: 6/6 seeds matching `interpretDesign`, four-cycle trace
agreeing, control interpreter agreeing, all six support conjuncts true, an
ACCEPTED `checkResidual` bound, and a derived zero exit, on every row.

**What this is NOT.** `--file-ab` runs the FORK specializer
(`ProtoFast.mixDriver`) and the PARTIAL interpreter variant (`hwAPVar`, from
the `partial` `goInline`, which has no equivalence lemma). Its agreements are
RUNTIME EVIDENCE and **do not inherit** the covered total variant's kernel
guarantees -- `IHwAdequate_varT` and `specializeDesign_varT_correct` are about
`hwAPVarT`, a different program.

### 42.5 A capture bug that would have overstated provenance

The first file-ab batch produced **zero** `.capture.json` files: I passed the
recorder a relative path against a changed `cwd`, and swallowed the
subprocess result. The rows would then have been recorded at `reconstructed`
strength while looking like pre-launch ones. The call now reports its failure
and the path is absolute; the batch was re-run with working captures, which is
running the procedure correctly after fixing a defect -- not manufacturing
provenance for an older run.

All nine are in `fp_master_experiments.jsonl` with
`code_identity.describes = "the code that ran"` and the exit from the unchanged
`/usr/bin/time` trailer. The frozen original manifest is untouched.

### 42.6 Coverage arithmetic

Of 26 old passes: **9 fully admitted** (file-ab rows 2, 3, 6-12), **8 observed
but evidence-limited** (total-probe S1 rows 15-22), **9 remaining** -- row 4
(total-probe `rt_alu`) and rows 23-30 (the S2 stage). Non-passes preserved as
recorded: rows 1 and 5 `exit 3`, rows 13 (host-var) and 14 (total-probe) exit
unknown. **An old `exit 3` is not an obligation to turn into a pass, and an
unknown exit is not an additional old pass.**

Still open: the S2 and row-4 total configurations; the complete runner, trace,
scaling, DCERT, census and fixture inventory; the CORE-ET/CVA6/DINO discovery;
and fresh DCERT2 FP transport, which stays open until the current parser plus
FP admission and runtime route execute. `pmp_data_if`'s fresh export remains
`EMIT_FAILED rc=6` and in the denominator despite its historical replay
passing under both runners.

## 43. FP checkpoint 6: the admission boundary, repaired and controlled

### 43.1 Seven admissions that should have been refusals

Retained CLI controls against `fp_replay.py` at
`456a7518c76c4724519c4f7268a7939f534246626992aced656ee05db59524b1` showed it
admitting a row with **no worker timing, no launch marker, ONE stimulus, ONE
trace cycle, a command naming a different binary, and a capture naming a
different binary**. Only a nonzero outer status refused. I wrote that admitter
as the fix for a join that asserted `exit=0` as a literal, and it was still too
permissive.

Four more defects beside those:

* the capture producer printed its failure and **launched anyway**, so a row
  with no capture at all could be admitted looking like a pre-launch one;
* `--admit-only` **re-invoked prelaunch**, mutating the very evidence it was
  meant to re-read;
* status and signal files were not scoped to an attempt, so an earlier run's
  `.exit` could be read as this one's;
* `wall_s` was the PARSER's elapsed time, so re-admission reported seconds that
  had nothing to do with the run.

### 43.2 What `pass` now requires

All of: an `.attempt` id with **every** evidence file carrying it; an outer
status present and uniquely parsed and zero; the worker's own `exit` trailer
present, uniquely parsed and agreeing; a launch marker with the log newer than
it; a recorded command naming this binary, this mode, this certificate and
BOTH fuel numbers; a prelaunch capture that parses, names this binary and
certificate by full sha256 and **predates** the launch marker; exactly the six
stimuli `0,1,2,3,4,7` with no duplicates, all matching; a four-cycle agreeing
trace; and the mode's support, checker and control contracts.

Beside that: `subprocess`'s NEGATIVE return for a signal is handled; a signal
stop is called a signal stop and is **only** called an OOM when retained cgroup
`memory.events` says `oom_kill` was nonzero; `wall_s` comes from a `.wall`
file written at launch; guard metadata is kept as REQUESTED and OBSERVED
separately with absent observations left absent; capture failure **fails
closed** without launching; and `--admit-only` launches nothing and takes no
capture.

One further bug the controls exposed: the recorded command contains a literal
newline (`-f "RSS %M KB\nexit %x"`), and writing it raw **split the join row in
two**, so a reader took the tail of the command as the first column of a second
record. Fields are escaped now.

### 43.3 Thirty-two controls, each on its own reason

`pass/lean/tests/fp/fp_replay_controls.py`, real CLI, isolated copies,
`--admit-only` throughout so nothing builds or runs: positive; missing,
malformed and DUPLICATED worker trailer; missing and malformed status; missing
launch marker; missing attempt; a foreign attempt file; missing, malformed,
post-launch and wrong-binary capture; wrong command; a command missing the fuel;
wrong input hash; wrong binary; stale log; missing, duplicated and disagreeing
stimulus; truncated and disagreeing trace; failed support, checker and control;
`variant not applied`; nonzero exit under success-looking output; timeout;
signal-not-OOM; `--admit-only` leaving all 16 evidence files byte- and
mtime-identical across two runs; and capture failure failing closed.

Each case asserts the REFUSAL REASON, not merely a nonzero exit -- a check that
only reads the status cannot tell "refused for the right reason" from "refused
for another". **All 32 pass.**

Two of them failed first on the FIXTURE rather than the admitter: the fixture
copied the binary, so the recorded command legitimately named a different path.
That is the admitter being right about a fixture that was wrong, and the
fixture now uses the real binary read-only.

### 43.4 What the repaired boundary says about the nine

Re-admitted from genuine retained evidence, **without re-running them**: 8 of
the 9 file-ab rows and all 8 total-probe rows now report
**`observed_evidence_limited`** -- their observables are all still there and all
still good (six matching stimuli, agreeing trace, agreeing control, six support
conjuncts, an ACCEPTED bound), but they predate attempt scoping, so they cannot
be AUTHENTICATED to a run and are not credited.

`--observed-only` is opt-in and can never return `pass`; by default missing or
malformed evidence refuses, which is what the controls check. Evidence-class
and observable-class refusals are separated into their own columns, because
"this cannot be authenticated" and "this did not do what the contract requires"
are different claims.

`pmp` is the exception, admitted at full strength -- I ran it once under the
repaired launcher to check that the repaired tool produces complete evidence
end to end, which is validating the tool rather than improving a label.

### 43.5 Corrected coverage

Of the 26 old passes: **1 fully admitted**, **16 observed but
evidence-limited**, **9 never attempted** (row 4 and the eight S2 rows). Rows 1
and 5 remain `exit 3` and rows 13 and 14 remain exit-unknown; neither is an
obligation to turn into a pass.

That is a smaller credited number than checkpoint 5 reported, and it is the
honest one. Nothing observed has been retracted -- the comparisons are retained
in the joins with their evidence paths -- but a row is credited only when the
evidence can be tied to the run that produced it.

## 44. FP checkpoint 7: row 4 and the eight S2 configurations

### 44.1 Five more binding defects, closed

Retained CLI controls against `fp_replay.py` at
`0d4d0dc574b2d2b85021bec1a365deb97f51ef0b3dee262e5e29b8b8183d6248` showed it
admitting rows with a **blank attempt id**, a **step fuel of 12,000,000
accepted as 200,000 by substring**, a capture whose `binary_sha256` and
`cert_sha256` were wrong but whose NOTES mentioned the right hashes,
`--allow-missing-capture` yielding a full `pass`, and a **malformed `.wall`**.

Each is now bound properly:

* attempt ids must match `\d{8}T\d{6}-[0-9a-f]{8}` and be nonempty -- a blank id
  compared equal to every blank sidecar, so unscoped evidence matched itself;
* the invocation is recorded STRUCTURALLY in `.argv.json` and checked **token
  by token in position**, because the wrapper command embeds a newline inside
  the `/usr/bin/time` format and cannot be recovered by splitting on
  whitespace -- which is also how `12000000` passed as `200000`;
* the capture is bound to its SCHEMA FIELDS -- `binary_sha256`, `cert_sha256`,
  `evidence == "captured-pre-launch"` -- never to a text search;
* an absent capture caps the row at evidence-limited **even when explicitly
  allowed**; the flag chooses between refusing and keeping an observation,
  never crediting;
* `.wall` must parse as a finite non-negative duration.

Nine narrowly targeted controls were added for exactly these, and the existing
suite is retained: **39 controls, all passing**, each asserting its own refusal
reason.

### 44.2 A latent bug the fail-closed guard surfaced

The first S2 attempt refused all eight rows with `CAPTURE_FAILED` and launched
nothing. The cause was ordering: the output directory was created inside
`launch`, which runs AFTER the capture, so against a fresh directory the
recorder had nowhere to write. The guard did exactly what it was built for --
a batch refused instead of eight runs proceeding uncaptured.

### 44.3 Which implementation carries which configuration

Master builds from the pinned FP head, which is **S3** ("deferred shifting
removes the shift quadratic"). The recorded binaries are different in every
case:

| rows | stage | recorded binary |
| --- | --- | --- |
| 15-22 | S1, `mixDriver` carrying `PRes.val` | `acc023ee...` |
| 23-30 | S2, the `nthD` lookup summary integrated | `1b5531a6...` |
| 4 | no S-stage; CVA6 `alu.sv` as `cva6_alu_export` | `9168c836...` |
| master | **S3** | neither |

So each replay is **this row's CONFIGURATION under master's S3 transport**, and
no replay is a recreation of an old binary. A run is counted toward its own
row only: the S1 rows and the S2 rows are the same eight inputs and are
attempted separately, because matching input names is not a shared result.

### 44.4 Results

Eight S2 rows, 1800 s each, and row 4 at 3600 s -- one finite attempt per
configuration, serial, under an 8 GiB observed guard, into fresh output
directories so no prior run was overwritten.

| row | block | status | wall | peak RSS | seeds | trace | control | checker bound |
| ---: | --- | --- | ---: | ---: | --- | --- | --- | ---: |
| 23 | instr_scan | pass | 9.82 s | 12,048 kB | 0,1,2,3,4,7 | 4c true | true | 5,711 |
| 24 | commit_stage | pass | 37.23 s | 11,968 kB | 0,1,2,3,4,7 | 4c true | true | 5,448 |
| 25 | compressed_decoder | pass | 9.28 s | 14,688 kB | 0,1,2,3,4,7 | 4c true | true | 8,962 |
| 26 | pmp | pass | 12.69 s | 15,440 kB | 0,1,2,3,4,7 | 4c true | true | 14,277 |
| 27 | pmp_data_if | pass | 34.81 s | 20,436 kB | 0,1,2,3,4,7 | 4c true | true | 19,743 |
| 28 | alu_wrapper | pass | 70.32 s | 23,128 kB | 0,1,2,3,4,7 | 4c true | true | 28,889 |
| 29 | alu | pass | 70.76 s | 23,828 kB | 0,1,2,3,4,7 | 4c true | true | 28,899 |
| 30 | decoder | pass | 133.19 s | 29,532 kB | 0,1,2,3,4,7 | 4c true | true | 38,754 |
| **4** | **rt_alu** | **pass** | **72.52 s** | 23,136 kB | 0,1,2,3,4,7 | 4c true | true | 28,899 |

**Row 4 is the one worth pausing on.** Its recorded run took **61,330.78 s** --
about seventeen hours -- and I budgeted 3600 s expecting a resource stop. It
finished in **72.52 s**. That is the S3 transport against a binary from before
the shift quadratic was removed, and it is a difference in the implementation
carrying the configuration, not evidence about the old run.

All nine carry a genuine pre-launch capture, attempt-scoped raw output, status,
timing and structured argv, and all nine are recorded through the original
`record_experiment.py` with the unchanged exit trailer. The master manifest now
holds **19 records**, every one at `"the code that ran"` strength.

### 44.5 Coverage

**Of the 26 old passes: 10 admitted, 16 observed-but-evidence-limited, 0
unattempted.** Rows 1 and 5 remain `exit 3` and rows 13 and 14 remain
exit-unknown -- preserved dispositions, not obligations to turn into passes.

The 16 are the earlier file-ab and S1 batches: observables intact, but recorded
before attempt scoping existed, so they cannot be authenticated to a run. They
were NOT re-run to relabel them.

Still required and open: the original runner, trace, scaling, DCERT, census and
fixture procedures; the CORE-ET/CVA6/DINO benchmark discovery; and fresh DCERT2
FP transport, which stays separate from the historical DCERT1 replay these rows
exercise. `pmp_data_if` passes historically under both runners and its fresh
export is still `EMIT_FAILED rc=6`, in the denominator.

## 45. FP checkpoint 8: the fixture and loader procedures, and the real inventory

### 45.1 `guard_observed = {}` was absence, and now is not

Every guarded run so far recorded `observed: {}`. That is the ABSENCE of an
observation, not evidence that the limit was enforced, and I had been writing
it beside a `requested` block as though the pair said something.

Two defects had to be fixed to get a real one. The sampler read the cgroup of
`proc.pid` -- but that is `systemd-run`'s CLIENT, which lives in the SESSION
scope, so it sampled the whole session: `memory.max "max"`, a 128 GB peak, and
reported it as this run's guard. Naming the scope with `--unit` makes the
cgroup path deterministic, and sampling must happen WHILE the scope exists
because it is torn down when the process exits. Observed now, for real:

```
requested: MemoryMax 8G, wall_timeout_s 1800, unit fpreplay-pmp-<attempt>
observed : memory.max 8589934592   memory.peak 13590528   memory.current 13459456
           memory.events low/high/max/oom/oom_kill/oom_group_kill all 0
           cgroup .../fpreplay-pmp-<attempt>.scope   scope_pids [3352443, 3352448, 3352449]
```

`memory.max` reads back as exactly the 8 GiB requested. Absent observations
are still left absent rather than backfilled.

### 45.2 The fixture and loader route

`run_cert.sh --emit` into a fresh project-local directory, then each fixture
through the historical DCERT1 loader at the original defaults (`4 1`):

| fixture | bytes | magic | exit | result |
| --- | ---: | --- | ---: | --- |
| `tiny` | 48 | DCERT1 | 0 | one cycle AGREES `OUT[1] 4:0`; trace AGREES (4 results); residual 48, bound 9 |
| `seq` | 82 | DCERT1 | 0 | one cycle AGREES `OUT[1] 4:13 | FLOP[1] 4:0`; trace AGREES (4 results); residual 149, bound 19 |
| `wide` | 162 | DCERT1 | **2** | **REFUSED**, by admission |

`seq` is the one that matters for threaded state: the output and the flop
next-state are both reported and both agree across four cycles, so this is
output AND state, not an exit code.

**`wide` is an ADMISSION refusal with its intended reason**, which is what the
checkpoint asks for and what a wrong-magic rejection cannot give: it parses
fine, `support` reports `memFree false` and `sources false`, and the runner
says *"the file has flopQAsync sources but v1 gives every flop asyncReset =
false, so this would simulate a DIFFERENT design."* That is the DCERT1 metadata
limitation refusing a body, not a format check.

### 45.3 The grammar refusals, with reasons rather than booleans

`CertLoad` carries these as `#guard rejects "..."`, which says a malformed
input is refused but not why.
`pass/lean/tests/fp/fp_dcert1_admission_controls.lean` prints what the parser
actually says, for all eight plus the minimal valid file:

```
empty (no magic)        : bad magic: expected DCERT1
wrong magic DCERX1      : bad magic: expected DCERT1
truncated, no counts    : malformed certificate
missing nodes count     : malformed certificate
unknown opcode 99       : malformed certificate
trailing garbage        : malformed certificate
source count > records  : malformed certificate
unknown source tag 9    : malformed certificate
minimal valid           : ACCEPTED
```

**A limitation worth stating rather than glossing.** The V1 parser produces
exactly TWO distinct reasons. Six structurally different faults -- truncation,
a missing count, an unknown opcode, trailing data, a count overrun, an unknown
tag -- all collapse to `malformed certificate`. Each is genuinely refused, and
the control proves that; what it cannot show is that the parser distinguishes
them, because it does not.

### 45.4 `ab_probe` at -O0 and -O2

Built twice from the same sources into isolated ROOTs -- `-O0` in 167.72 s
(`4bbf1195...`), `-O2` in 214.61 s (`9216322f...`) -- and each run once under
an 8 GiB named scope with a 1800 s cap. Both exit 0, and the FULL original
procedure ran: nothing was shortened.

Sequential: reset `next==0`, enabled `next==12`, held `next==3`, the
`runtimeShape` refusal, and a 3-cycle trace with `len3`, `next-states-ok` and
`ref-equal`. Node ladder at n = 8, 16, 32, 64, 128 and source ladder at
nsrc = 16, 64, 256, 1024, 4096 (nodes = 64), each checking HOST
(`mixDriver` + `hwAPVarT`) and FORK (`ProtoFast` + `hwAPVar`) at their own
accepted bounds, with `terms` equal and `same-size true` at every point.

**Every result, value and shape field is IDENTICAL across the two builds.**
Only timing and memory differ, and those are kept separate:

| | host @4096 | fork @4096 | peak RSS |
| --- | ---: | ---: | ---: |
| `-O0` | 9,600 ms | 25,283 ms | 66,876 kB |
| `-O2` | 1,049 ms | 2,573 ms | 15,800 kB |

The fork route stays execution evidence: `ProtoFast` + `hwAPVar` comes from the
`partial` `goInline` and has no equivalence lemma, so it is not promoted
through the total variant's theorems.

### 45.5 The inventory, including where FP has nothing

`fp_entrypoint_inventory.tsv` freezes all 18 original entrypoints with each
one's sha256, the modes parsed from its OWN argument dispatch, what it asserts,
its original status and master's evidence: **8 executed, 1 partial, 8 open, 1
adapted-but-not-separately-run**. The modes column is a denominator in itself --
`proto_probe` alone dispatches 22 modes and two have been exercised.

`fp_obligation_inventory.tsv` records the per-corpus denominators, and its
point is the column that says where the original has nothing:

* **CVA6** -- 11 distinct designs across the 30 rows. Real evidence. 10
  admitted, 16 evidence-limited, 0 unattempted.
* **CVA6 frozen-30** -- FP never ran the frozen 30 as a set. There is no FP
  denominator here, and D3's or D4's accepted subset is not FP coverage.
* **CORE-ET** -- FP has a CENSUS only: `coreet_operator_census.py` reports
  which operators the CORE-ET certificates use and checks fixed arities. It
  simulates and specializes nothing. **FP has no CORE-ET execution evidence**,
  and the census has not been run on master.
* **DINO** -- nothing. DINO appears only as a scaling LIMIT:
  `HardwareInterpreter.lean` and `Scaling.lean` both put the practical ceiling
  at roughly N = 1000-2000 slots and note that DINO's 4,772-node
  `SingleCycleCPU` is already past it, and `SIMULATOR_PLAN.md` records
  real-design evaluation as **Not started**.

### 45.6 Open

The scaling and prototype grids (`s0_grid`, `s1_identity`, `s2_probe`,
`s2a_probe`, `s3_probe`, `perf_probe`), the remaining 20 `proto_probe` modes,
`cert_shape.py`, `cva6_corpus_audit.py`, the CORE-ET census, real `cert_runner`
benchmark passes beyond `instr_scan`, and fresh DCERT2 transport -- all open
until executed. `pmp_data_if`'s historical acceptance stays separate from its
fresh `EMIT_FAILED rc=6`.


## 46. FP checkpoint 9: five prototype modes, and a corpus the FP prototype cannot read

### 46.1 What ran

Five `proto_probe` dispatch modes were executed on master, serially, each under
a named 8 GiB scope with a 1800 s cap, from the pinned binary
`ec40994e40e625ecab040bc561d697c3b2aadc254a3e284ad44bedfe9cbe488f`. Raw output,
exact argv, wall and peak RSS are retained per run under
`generated/master_port/fp/proto_modes/` and tabulated in
`pass/lean/tests/fp/fp_proto_mode_ledger.tsv`, one row per dispatch form.

`--reset-probe` was driven on a genuinely sequential fixture, not a zero-flop
case: `seq.dcert`, one flop with an enable and a synchronous reset. It asserts
reset for two cycles, releases, and measures only the six cycles after release:
**3 distinct post-release states**. The per-cycle line alternates
`moved`/`initial`, but that label only compares against the initial state, so
`moved` covers more than one state and the distinct count is the number that
matters. The progression is non-vacuous -- the state moves after the release,
so the fixture is not reset-stuck.

`--trace-ref seq.dcert 4` reported **1 distinct state**, which at first looks
like the vacuous case the mode exists to detect. It is not. The per-cycle
stimulus is `patternAt` seed `k`, and on this fixture seeds 0..3 drive either
the enable low or the reset high at every one of the four cycles, so the flop
holds. Re-running the same fixture at 16 cycles gives **state changed on 4, 3
distinct states**. The 4-cycle reading was stimulus length, not the design, and
the mode's own text already refuses to call 1 state degenerate.

`--support` was run on positives and on the intended refusal. `tiny.dcert` and
`seq.dcert` report all six `SupportedByProjection` fields true. `wide.dcert` is
refused, and the printed reason is **`memFree false` and `sources false`** --
it carries a `memImg` and a `memConst` source. That is not where I expected the
refusal: the standing note said `wide` refuses on the async/clock field. It does
not; `wf`, `ops`, `arities` and `flopClocks` are all true. The exit code is also
weaker than it looks -- `--support` returns 0 if ANY file in the invocation is
supported and 2 if none, so a zero exit is a disjunction over the argument list,
never a per-file verdict. Only the printed fields are per-file.

`--inline-negative` ran on its actual counterexample: `found true`,
`useInNext 1`, `nextStrict true`, `beforeTotal FALSE`, `applied false`. Before
the rewrite the outcome is `typeError`; with the rewrite FORCED it is
`outOfFuel`. The checker refuses it and the two outcomes differ, so the side
condition is load-bearing. One counterexample shows the guard is necessary, not
that the guard set is complete.

`--env0-ab`, the strict acceptance command, exits 0 with every case and
assertion its dispatch defines: the rewrite applies with the intended flags,
the variant resolves (`bta`, `wfA`, `erase-ok`), **8 designs x 6 stimuli all
agree**, an out-of-range dep yields `typeError` on both sides (same kind,
neither a success), five traces over six cycles agree with states-reached
4/4/2/1/1 (`flopD 8`, `flopD 32`, toggle, hold, reset-only), and seven
projection A/B rows agree with `tl` collapsing 4033 -> 1, 28545 -> 1,
61313 -> 1 and 126849 -> 1 on `cstD`, and 61184 -> 32640 on `srcD`. Both sides
run the same fork specializer, so this is interpreter-vs-interpreter agreement,
not a bridge to the proved backend.

**Mode tally: 7 of 20 dispatch forms executed** (`--file-ab` and
`--rewrite-agree` earlier, these five now). The grid, depth, profile, host-var
and remaining file modes are **not** credited and are listed as NOT EXECUTED.

### 46.2 `--reset-probe` quarantined two real designs, and the quarantine is right

Run on real imported CVA6 certificates the mode exits 1:

```
ariane_regfile_gate.dcert: 32 flops
  reset pins -- sources say [(2, true)], flop records say [(2, false)]
  !! POLARITY CONFLICT on input 2 ...
serdiv_gate.dcert: 12 flops
  reset pins -- sources say [(8, true)], flop records say [(8, false)]
```

`formal/lean/scripts/cva6_corpus_audit.py`, which matches by FLOP IDENTITY
rather than by pin set, agrees: `CONFLICT=32` and `CONFLICT=12`, every flop.
Across the frozen 30 it is **21 sequential blocks, 21 with a conflict, 16 with a
conflict on every flop, 0 unknown**.

That looked like a corpus-wide defect, and `DirectCheck.lean` already says it is
not -- it declines to cross-check reset polarity and gives the reason:

> an async flop's `resetPin` slot and its `resetInput` ordinal legitimately
> differ in POLARITY -- the census shows 9,262 async sources whose `activeLow`
> is the opposite of their `FlopDesc.resetActiveLow`, because the pin slot reads
> an already-inverted node while the ordinal reads the raw port.

That justification is **conditional on the pin slot being a node**, and the
condition is checkable. `formal/lean/scripts/reset_polarity_census.py` is new
and checks exactly it; `pass/lean/tests/fp/fp_reset_polarity_census.tsv` is the
result, and it is also the only retained artifact for a census of this kind --
the 9,262 figure arrived with the D2 port (`106531b2e`) and nothing in this tree
reproduces it.

| corpus | schema | certs | node-mediated | raw-port OPPOSITE |
|---|---|---|---|---|
| imported pre-D2 (`cva6_30_auth`, `cva6_30_auth2`) | DCERT1 | 59 | **0** | **5,837** |
| master-emitted (`generated/master_port/**`) | DCERT2 | 132 | 2,195 | **0** |

In the imported corpus **every** reset-bearing flop points `resetPin` at a raw
`.input` -- never a node -- and carries `activeLow = 0`, while the same port's
`flopQAsync` source carries `al = 1`. In `cva6_30_auth` alone that is 2705 of
2705 flop rows at 0 and 2673 of 2673 source rows at 1, with no counterexample on
either side. Both sides read ONE port with OPPOSITE polarity
(`Runtime.lean:83` against `DesignSemantics.lean:67`), so no value of that port
leaves both paths out of reset, and DirectCheck's reason does not cover it.

Master's output is the shape DirectCheck describes: `resetPin` resolves to the
already-inverted node, 2,195 times, with zero oppositions. It cannot be
otherwise -- `certificate_ir.cpp:72` and `:271` both assign the single scanned
`f.active_low`, and `emit_dcert.cpp:169` and `:231` both emit the single
`reset_active_low`, so the two fields are structurally incapable of disagreeing.

**Classification: a pre-D2 export-boundary shape, confined to the imported
DCERT1 corpora, not reproducible by master's writer.** Not an RTL bug, not a
projection bug, and not something to repair by editing certificate literals --
which is what the probe's own quarantine text says.

**Impact on results claimed so far: none.** Every design in the FP evidence --
`instr_scan`, `pmp_data_if`, `pmp`, `decoder`, `compressed_decoder`,
`commit_stage`, `alu_wrapper`, `alu` -- has **0 flops**, so no flop reset
semantics ever executed in an admitted FP run. Every master-port D3/D4 corpus is
DCERT2 with zero oppositions. Differential results are unaffected in any case:
both sides of an agreement consume the same certificate. What the opposition
does weaken is any reachability reading of a sequential run over the imported
corpus, including the two `--trace-ref` runs in 46.1, because one reset path is
asserted throughout.

### 46.3 Two limitations of `--reset-probe`, observed not repaired

A control certificate was written for this, tracked as
`pass/lean/tests/fp/fp_reset_polarity_control.dcert` and run from the
byte-identical `generated/master_port/fp/fixtures/dcert/diffpin.dcert`: its read side
names input 0 and its commit side names input 1 -- a genuine mismatch. The mode
prints `sources say [(0, true)], flop records say [(1, true)]` and **exits 0**.
Its scan is `if i == j && al != bl` (line 568): a same-index polarity test over
pin sets, so a different-pin mismatch passes it. A clean `--reset-probe` exit is
therefore not evidence that a certificate's two reset records agree. The census
script classifies this case as `raw-port:different-pin`, and the control is the
only input in the tree that exercises that branch.

Second, `drive` asserts and releases the reset per the SOURCE polarity alone. On
a certificate carrying the opposition, the source's "released" is the commit
side's "asserted", which is why `ariane_regfile` and `serdiv` report a constant
post-release state and the mode labels it INCONCLUSIVE rather than claiming
unreachability. The probe cannot release both paths at once on those inputs.

`proto_probe.lean` is pinned prototype source and neither limitation was
repaired here.

### 46.4 The FP prototype's reachable corpus is DCERT1-only

`proto_probe --support` on a master-emitted certificate fails with
`bad magic: expected DCERT1` -- an uncaught exception, exit 1, distinct from
`--support`'s own exit 2. `CertIO.V1.loadCert` reads DCERT1 and nothing else.

The consequence is a real scope limit on this stage, and it explains 46.2's
impact line from the other side. `--support` reports `ariane_regfile` and
`serdiv` SUPPORTED with all six fields true, so the FP prototype's loadable
corpus **does** contain sequential CVA6 designs -- but every one of them is a
pre-D2 DCERT1 certificate carrying the raw-port opposition, and the
opposition-free certificates (master's DCERT2) are the ones it cannot read. So
today the prototype can exercise sequential reset semantics only on hand-written
DCERT1 fixtures such as `seq.dcert`. Carrying FP onto master-emitted sequential
designs needs DCERT2 transport, which remains open.

### 46.5 What the frozen 30 can even be projected onto, and a parser cross-check

`--support` over the 29 frozen-30 certificates present in `cva6_30_auth`, one
invocation, 0.39 s, peak RSS 33,152 KB, exit 0: **22 SUPPORTED, 7 refused.**
All seven fail `ops`. Five also fail `sources` -- `aes`,
`cva6_hpdcache_subsystem`, `fpu_wrap`, `load_unit`, `store_unit`. Only
`cva6_hpdcache_subsystem` fails `memFree`. `wf`, `arities` and `flopClocks`
hold on all seven. So FP's ceiling on this set is **22 of 30 before anything is
projected**, and that is a loadability measurement, not a run: nothing was
projected or compared here. Recorded in `fp_obligation_inventory.tsv`.

The 5,837 figure in 46.2 comes from a new parser, so it was cross-checked
against two independent readers on the same 29 files. `reset_polarity_census.py`
agrees with the Lean loader's own printed `src N node M flop F` on all 29, and
with `cva6_corpus_audit.py`'s separate parser on all 29: **0 mismatches** on
`(sources, nodes, flops)` either way. The one apparent mismatch during this
check was mine, not the parser's -- `cva6_ptw_gate.dcert` exists in both
`cva6_30_auth` (3423/3774) and `cva6_30_auth2` (3424/3775), and keying the
comparison by basename collapsed them. Keyed by path, they agree.

### 46.6 Open after this checkpoint

Unchanged: the scaling and prototype grids (`s0_grid`, `s1_identity`,
`s2_probe`, `s2a_probe`, `s3_probe`, `perf_probe`), the thirteen uncredited
`proto_probe` modes, real `cert_runner` benchmark passes beyond `instr_scan`,
and fresh DCERT2 FP transport. `pmp_data_if`'s historical acceptance stays
separate from its fresh `EMIT_FAILED rc=6`, which stays in the denominator.

## 47. FP checkpoint 10: the six scaling probes, and what one build cannot show

### 47.1 All six built and ran, and nothing in them is false

The six entrypoints still marked OPEN -- `s0_grid`, `s1_identity`, `s2_probe`,
`s2a_probe`, `s3_probe`, `perf_probe` -- were built with the original
`scripts/build-opt.sh` at `-O2`, each into its own ROOT (202-255 s, 1.6-1.9 GB
each, all rc=0), and run serially under named 8 GiB scopes with a 1800 s cap.
**17 runs, every one exit 0.** Per-run argv, wall, peak RSS and the raw output
are retained under `generated/master_port/fp/probe_runs/` and tabulated in
`pass/lean/tests/fp/fp_probe_runs.tsv`.

None of the 17 run logs contains the string `false`, nor `FAILED`,
`REJECTED`, `OUT OF FUEL` or `NOT SupportedByProjection`. Every grid ends
`ALL CHECKS PASSED: true`.

### 47.2 The identity result, on a real design

On `instr_scan_gate.dcert` (1235 sources, 1264 nodes, 0 flops) three different
specializers -- the proved `Projection.mixDriver`, the S2a summary prototype
`ProtoSum.mixDriver`, and the S3 deferred-shift prototype
`ProtoShift.mixDriver` -- all produce

```
48439 terms   checker bound 5711   digest 16712272993486204376
```

and in every case the checker accepts, all six width-aware stimuli match
`interpretDesign`, and the four-cycle trace agrees. The earlier `total_probe`
row-15 record agrees independently (48439 terms, bound 5711, 741 ms).

Specialize time on that input: host **745 ms**, `ProtoSum` **710-742 ms**,
`ProtoShift` **162 ms** -- a **4.6x** reduction with a residual the digest
cannot distinguish.

`identical`/`same` in these probes is `digest A == digest B`, a 64-bit
structural hash over the whole residual term -- constructor tag, literals,
variable indices, primitive codes, constructor names, case alternatives, call
targets. Equal digests are strong evidence of an identical residual, not a
proof of one, and the probes' own wording is kept.

### 47.3 What the grids say

| grid | points | identity | timing |
|---|---|---|---|
| `s2a_probe grid` | 25 | 25/25 identical | `ProtoSum` tracks the proved host within noise, and is SLOWER on the all-dynamic points (dyn S=256: 123 -> 175 ms) |
| `s2_probe grid` | 25 | 25/25 identical | same experiment on this build (see 47.4) |
| `s3_probe grid` | 25 | 25/25 identical | `ProtoShift` is 3-8x the host on the static sweeps (D S=N=2048: 2767 -> 342 ms) and ~1x on the all-dynamic points |
| `s0_grid` | 20 | 11 host-anchored points all `same true` | the FORK is 7-11x SLOWER than the proved host where both ran; largest fork point D S=N=1024 at 6259 ms |
| `s1_identity` | 18 | per-point HOST terms + digest recorded | the proved host is 2-9x FASTER than the fork (gridD S=N=512: 182 vs 1598 ms) |

Every point of every grid reports `chk true ref true` on both backends: the
fragment checker accepted and the residual matched `interpretDesign` at its own
checked bound, outside the timed section.

Cross-probe, from independent binaries and independent builds: `s2-grid` and
`s3-grid` cover 20 distinct `(S, N, E)` coordinates in common and agree on
**both** term count and digest at all 20. `s0-grid`'s fork column and
`s2-grid`'s host column share 15 coordinates and agree on term count at all 15
-- the two apparent mismatches are coordinate collisions, not disagreements,
because `s2`'s `dyn` sweep reuses `(64,64,128)` and `(256,64,128)` with every
source dynamic.

### 47.4 One build cannot run a two-build experiment, and `s2` = `s2a` here

S1, S2 and S3 are specified as *one source, built twice*: once against a tree
whose proved specializer is S1's (`0f175676b`) and once against the current
tree. In all three probes, though,

```lean
def specS1 (sf wf : Nat) (D : DesignCert) : Except MixError Program :=
  mixDriver sf wf hwAPVarT [encDesign D]
```

-- the label `s1`/`host` is **build-relative**, not a pinned implementation.
Master's five specializer files (`PartialEvaluator`, `PartialEvaluatorCorrect`,
`MixProgram`, `Proto/PartialEvaluatorSummary`, `Proto/PartialEvaluatorShift`)
are byte-identical to the S3 pin, and the last two **do not exist at all** at
`0f175676b`. So on master's single build every `s1`/`host` column is the same
code, and the cross-build S1-vs-S2 comparison is **not executed and not
credited**.

That also means `s2_probe` and `s2a_probe` are the **same experiment** on one
build. Diffing their sources with the probe names normalised away leaves 36
lines: prose, labels, the backend keyword (`host` vs `s1`) and one extra
printed field (`s2` prints the HOST digest). Both compare `mixDriver` against
`ProtoSum.mixDriver`. Running both is a reproducibility check, not two results,
and the measured numbers bear that out -- 25/25 identical in each, HOST digests
equal at every shared coordinate.

It also explains the one result that looks disappointing: `ProtoSum` shows no
speedup over the host. On a build whose host **already contains** the
integrated summary, there is nothing for the prototype to beat. That is the
cross-build comparison, and it is exactly the half that is missing.

The S1 tree is reachable: `0f175676b` and the pin `9bb41a09d` are both in
`/WORKROOT/projects/livehd-futamura/livehd`, branch `projection`.
Building against it is a branch-history performance study, not a statement
about master's behaviour, so it is left open rather than attempted here.

### 47.5 `perf_probe`'s RSS column is not a peak

`perf_probe` reports, within one process, `9104, 9116, 9136` then `8408` kB --
it goes **down**, which no high-water mark does. Sampling
`/proc/<pid>/status` from outside the same run reproduces the
non-monotonicity: max `VmHWM` observed 9176 kB, final 8408 kB, and
`/usr/bin/time %M` 8412 kB. So the column tracks residency at print time rather
than a peak and is not comparable with `%M`. The mechanism was not pinned down
and is not guessed at here. Every RSS figure in `fp_probe_runs.tsv` is the
guarded runner's `%M`; no claim rests on the probe's own column.

`perf_probe` also always returns 0, so its exit code carries no verdict -- the
content does. chainD specialize runs 4/11/19/37/140/621 ms at n = 16..512 with
residual size `15n+33` and bound `3n+5`; fanD, where every node reads both
source slots at maximum depth, runs 6/13/22/61/259/1184 ms with size `16n+32`
-- about twice chainD at equal n, which is the slot-read-depth cost the probe
exists to isolate.

### 47.6 The last entrypoint, and what -O0 costs

`build-native.sh` was the one entrypoint with no execution record of its own.
It had none for a reason that only became visible in 47.7 below: it writes into
`.olean-dev`, and three FP commits had left that directory TRACKED, so running
it would have rewritten 78 MB of committed binaries. With both output
directories ignored, it runs cleanly and leaves the worktree untouched:
**rc=0, 153.42 s, 1.6 GB, 36 modules plus the probe**, producing
`.native-dev/perf_probe`. That makes the entrypoint inventory **18 of 18
executed**.

The script exists to separate "the interpreter is slow" from "the specializer
is slow", and running its binary against the `-O2` one on the same ladder
answers that directly:

| n | 16 | 32 | 64 | 128 | 256 | 512 |
|---|---|---|---|---|---|---|
| `build-native` (default) | 26 ms | 30 ms | 88 ms | 341 ms | 1507 ms | 7716 ms |
| `build-opt -O2` | 4 ms | 11 ms | 19 ms | 37 ms | 140 ms | 621 ms |
| ratio | 6.5x | 2.7x | 4.6x | 9.2x | 10.8x | 12.4x |

Residual size and checker bound are **identical at every point** (273, 513,
993, 1953, 3873, 7713 and 53, 101, 197, 389, 773, 1541). Optimisation level
changes the time, not the residual, and the penalty widens with n.

That run also sharpens 47.5: the `-O0` binary's RSS column **is** monotone
(9564 ... 16852 kB) yet still ends *above* that run's `%M` of 15,796 kB. So the
column is unusable as a peak in both builds, for two different reasons.

### 47.7 Two repository findings from the same pass

**The pin is not reachable from this repository.** `9bb41a09d` is the head of
branch `projection` in `/WORKROOT/projects/livehd-futamura/livehd`;
`git cat-file` fails on it here. `fp_entrypoint_inventory.tsv` claimed its
`pinned_sha256` column was read with `git show` without saying where, which is
not reproducible. The header now names the repository and the exact command.
All 18 entrypoint rows and all 56 `fp_install_map.tsv` rows were then
re-verified against it, independently of the installer: **0 pin mismatches, 0
drift since install, 0 installed/read-back disagreements, 0 `verbatim`-flag
errors.**

**78 MB of build output was committed.** `formal/lean/.olean-dev` is where
`.build-proj.sh` writes its oleans and `run_cert.sh` reads them back through
`LEAN_PATH`; `.native-dev` is `build-native.sh`'s output, which that script
calls a diagnostic nothing in the project depends on. Neither is among the
install map's 56 assets. Three FP commits -- `73dee5673`, `df8b7c8ec`,
`d8d84afbd` -- nevertheless swept 46 `.olean` files, 78 MB of a 208 MB pack,
into the repository. They are untracked again and both directories are now
ignored; the files stay on disk and nothing tracked reads them. The blobs
remain in those three commits, and dropping them from history would need a
rewrite, which is not done here.

## 48. FP checkpoint 11: the thirteen uncredited prototype modes

Checkpoint 9 ran five `proto_probe` modes and explicitly refused to credit the
other thirteen. They are run here: **19 further guarded invocations, every one
exit 0**, which takes the mode ledger to **20 of 20 dispatch forms executed**
and `fp_entrypoint_inventory.tsv` to **18 of 18 entrypoints**.

### 48.1 Decidable equality, on a real design and on a sequential one

`--file` does not compare digests. Its `identical` field is `R == F` --
decidable equality on `Program`. On `instr_scan_gate.dcert`:

```
ref size 2335903  fast size 2335903  identical true  same-result true
```

so `Hw.projectDesign` and `ProtoFast.mixDriver` produce **the same residual**,
term for term, on a real 1235-source, 1264-node CVA6 design, and one run of
each agrees. That is the strongest identity evidence anywhere in the FP set --
everything in section 47 rests on a 64-bit digest.

The bare shape form extends it to **11 synthetic cases**, all
`identical true  same-result true  ref-ran true  fast-ran true`: chain at
273/513/993/1953, fan at 288/544/1056/2080, and **flop at 433/1217/3937**.
`flop` is a sequential fixture, so this is decidable-equality identity on
sequential designs -- built by hand, and therefore free of the imported
corpus's reset-polarity opposition from section 46.

### 48.2 The interpreter, not the specializer, decides the residual size

Four runs on the same certificate, same fuel, pin this down:

| specializer | interpreter | residual |
|---|---|---|
| `Hw.projectDesign` (`--file-ref`) | `Hw.hwAP` | 2,335,903 terms |
| `ProtoFast.mixDriver` (`--file-fast`, stock 20000/200) | `Hw.hwAP` | 2,335,903 terms |
| `mixDriver` (`--host-var`) | `hwAPVar` | 48,439 terms |
| `mixDriver` (`--host-var-total`) | `hwAPVarT` | 48,439 terms |

A **48x** difference, decided by the interpreter alone. `--file-fuel` at ten
times the fuel returns the same 2,335,903, so the stock budget was not binding
on this input. `--host-var` and `--host-var-total` agree on every figure --
terms, `checkResidual` bound 5711, prim histogram, and seeds 0/1/4 each
matching `interpretDesign` -- which is evidence that the `partial` and total
variants agree here, not a transfer of theorems between them. Both modes print
their own scope: three seeds, no trace, no control, **feasibility evidence and
not the six-stimulus acceptance**.

### 48.3 Two measurements that close documented gaps

**`cert_shape.py`'s predictor, checked outside its validated mix.** The
predictor's own docstring says it was fitted on `rt_intpipe_alu` to 0.015% and
that it is "NOT established for ops this predictor has never been checked
against". For `instr_scan` it predicts **761,995** residual `tl`;
`--file-profile` measures **772,376** in the 2,335,903-term residual. The
predictor lands **1.36% low** on a design outside its validated set. Both sides
are `Hw.hwAP`, so the comparison is like for like -- the variant interpreter's
48,439-term residual (`tl` 10,381) is a different regime the predictor does not
model.

One observation is recorded without an explanation: the shortfall is
`772,376 - 761,995 = 10,381`, which is **exactly** the variant interpreter's
`tl` count on the same certificate. Equality to the digit is unlikely to be
chance and suggests the two quantities decompose the same chain, but nothing
here establishes that, and it is left as a question rather than a claim.

**`--node-depth`'s negative result, reproduced.** Across `midD` at
nnode = 64, 128, 256, 512, 1024 the node-portion read-depth sum grows from
2,016 to **523,776** while the residual's `tl` count stays at **exactly 1** at
every point. Deps that target node results cost nothing in the residual, which
is the measurement `cert_shape.py` cites for its chain (B).

### 48.4 `--depth-grid`: dynamism beats depth at scale

21 points, three families, `nnode` held at 64 throughout so node count cannot
explain anything:

| nsrc | `cstD` constant, deep | `shalD` shallow | `srcD` dynamic, deep |
|---|---|---|---|
| 16 | 3,806 / 961 | **1,203 / 121** | 4,205 / 1,080 |
| 128 | 37,854 / 12,161 | **26,011 / 8,129** | 62,613 / 20,288 |
| 256 | 87,518 / 28,545 | 100,443 / 32,641 | 186,197 / 61,184 |
| 1024 | **385,502 / 126,849** | 1,579,227 / 523,777 | 1,959,893 / 650,624 |

(terms / `tl`.) Shallow reads are the **cheapest** family up to nsrc = 128 and
the second most expensive by nsrc = 1024, crossing `cstD` between 128 and 256.
Making the sources static is worth more than making the reads shallow once the
source count is large: at nsrc = 1024 `cstD` is 4.1x smaller than `shalD` and
5.1x smaller than `srcD`. `--grid` adds 11 `srcD` points on the orthogonal
(nsrc, nnode) ladder, largest 58,229 terms at 64/256.

All three of these modes produce profiles only -- no reference comparison and
no execution of the residual -- and are recorded as such.

## 49. FP checkpoint 12: the first sequential CVA6 results

Section 46 ended with an awkward fact: every design in the FP evidence had
**zero flops**, so no admitted FP run had ever executed a flop reset or a state
update. That is fixed here.

`proto_probe --support` accepts 22 of the 29 frozen-30 certificates present in
`cva6_30_auth`. Eight of those already had FP evidence, and **all fourteen of
the rest are sequential**. All fourteen were run on the covered backend --
`total_probe`, `Projection.mixDriver` + `hwAPVarT`, the pair for which
`IHwAdequate_varT`, `specializeDesign_varT_correct` and `simSound_varT` are
proved -- at fuel 200000/2000, under an 8 GiB named scope with a 1800 s cap,
through `fp_replay.py` and its forty-control admission boundary.

**8 of 14 admitted**, every one sequential:

| design | flops | sources | residual terms | bound | wall s | states reached | status |
|---|---|---|---|---|---|---|---|
| serdiv | 12 | 1,082 | 79,736 | 5,291 | 9.99 | 2 | pass |
| ariane_regfile | 32 | 1,034 | 169,669 | 4,570 | 12.75 | 3 | pass |
| perf_counters | 12 | 1,652 | 109,653 | 7,977 | 31.57 | 2 | pass |
| cva6_ptw | 11 | 3,423 | 279,958 | 16,721 | 89.54 | 3 | pass |
| instr_queue | 71 | 1,105 | 388,119 | 5,075 | 189.14 | 3 | pass |
| store_buffer | 62 | 1,331 | 386,532 | 6,091 | 242.11 | 2 | pass |
| btb | 64 | 1,784 | 415,935 | 8,075 | 1079.03 | 2 | pass |
| id_stage | 30 | 10,503 | 1,600,933 | 48,805 | 1377.08 | 2 | pass |
| issue_read_operands | 89 | 6,172 | 2,473,580 | 28,329 | 1800.28 | -- | wall cap |
| bht | 256 | 7,372 | 6,118,005 | 33,199 | 1800.11 | -- | wall cap |
| scoreboard | 243 | 9,396 | 7,891,050 | 42,765 | 1800.28 | -- | wall cap |
| frontend | 409 | 13,466 | 20,259,844 | 61,316 | 1800.11 | -- | wall cap |
| issue_stage | 332 | 14,686 | 20,470,076 | 67,410 | 1800.22 | -- | wall cap |
| csr_regfile | 136 | 32,853 | 28,301,964 | 157,185 | 1800.21 | -- | wall cap |

Each admitted row required all six width-aware stimuli to match
`interpretDesign`, a four-cycle trace to agree, the checker to accept, and the
control interpreter to match -- plus the attempt binding, the uniquely-parsed
status and the prelaunch capture the controls enforce.

### 49.1 The six stops are time, not memory, and are not failures

Every stop is `exit 124` from `timeout`, recorded as **RESOURCE STOP,
undecided**. None is called a failure and none is called an OOM, because the
retained cgroup evidence says it was not one: `memory.events.oom_kill` is **0
on all fourteen**, `memory.max` reads back as exactly 8 GiB, and the worst peak
was `csr_regfile` at **1.29 GB** -- 15% of the budget. All six had already
finished specializing when the cap fell; they were in the stimulus phase.
All fourteen report `support_all true`.

### 49.2 What separates admitted from stopped is residual size

Not flop count, and not source count. Every admitted design has a residual of
at most **1,600,933** terms; every stopped one has at least **2,473,580**.
`id_stage` carries 30 flops and 10,503 sources and is admitted at 1377 s, while
`issue_read_operands` carries 89 flops and 6,172 sources and is not -- so
neither dimension separates the two groups on its own. What flop count does
drive is terms per source: `id_stage` yields 152 terms per source at 30 flops,
`bht` 830 at 256.

`scoreboard` reappears here as a stopped design, as it did in the D3
`scoreboard_gate` timeout at 3603 s. Different workload, same design class on
the hard end.

### 49.3 The scope these results do and do not have

These are pre-D2 DCERT1 certificates, so every one carries the raw-port
reset-polarity opposition of section 46. The **agreement** results are
unaffected: the residual and `interpretDesign` consume the same certificate, so
both sides apply the same inconsistent polarity, and their agreement is a
statement about the specializer, not about the polarity. What is weakened is
the reading of `states-reached`: 2 or 3 states over four cycles is **not**
behavioural coverage of these designs, because one reset path is asserted
throughout. The column is reported as an observation, not as coverage.

No rows were added to `experiments.jsonl` for these runs.
`record_experiment.py` reads the exit status from a `/usr/bin/time` trailer
inside the log, which `fp_replay.py` does not write -- it keeps the status in a
separate attempt-scoped `.exit` file. Recording them would have produced rows
marked `exit None, MISSING -- the log carries no exit evidence; status is
UNKNOWN`, which is weaker evidence than the join, not stronger. The join in
`pass/lean/tests/fp/fp_frozen30_seq_join.tsv` carries the derived outer and
worker status, the attempt binding and the guard observations instead.

**CVA6 FP coverage now stands at 8 combinational + 8 sequential designs**, and
at 16 of the 22 supported members of the frozen 30.

## 50. What the projection buys, and a final-acceptance criterion that cannot run

### 50.1 The speedup, measured on sixteen designs

Every `total_probe` run times the same six stimuli twice: `reference-runs` is
six calls to `interpretDesign`, `step-runs` is six calls to the specialized
residual. Both are native `-O2` Lean in one process, so the ratio is the
partial-evaluation speedup and nothing else. Across the sixteen admitted CVA6
designs (`pass/lean/tests/fp/fp_speedup.tsv`):

| | range | mean |
|---|---|---|
| **sequential** (8 designs) | **17x to 97x** | **49x** |
| combinational (8 designs) | 0.9x to 22x | 5.9x |

The top of the table is `instr_queue` at 97x, `btb` at 90x and `store_buffer`
at 87x; the two wall-capped designs show larger ratios still in their completed
stage lines -- `issue_read_operands` at 194x (507,691 ms against 2,623 ms) and
`bht` at 172x.

The bottom of the table matters as much. `pmp` is **0.9x**: its residual is
*slower* than the interpreter. `alu` and `alu_wrapper` are 1.7x, `decoder`
2.1x. On combinational designs the projection is marginal, and on one of them
it is a small loss.

Three limits on reading those numbers. It is a Lean-to-Lean ratio, not an RTL
simulation figure and not a comparison against any external simulator.
`specialize` is paid once and is reported separately -- on `alu` it is 18.9 s
against a 9.8 s reference run, so the ratio is a per-stimulus steady-state
figure, not an end-to-end win on a single run. And the sequential designs here
are the imported DCERT1 certificates of section 46, so what is being timed is
the same arithmetic on both sides, whatever the reset polarity does.

### 50.2 Section 6.4 cannot be executed as written

> **CORRECTED IN SECTION 51.** The claim below -- that no original branch has a
> generator -- is WRONG. It searched only for master's Direction-2
> `make_sim_launcher.py` / `Sims/` route. All three pins emit Lean simulator
> source from a native C++ emitter, `pass/lean/pass_lean.cpp`. The table in this
> subsection is accurate about `make_sim_launcher` and `Sims/` specifically and
> about nothing else; read section 51 for the real artifact map. The
> determinism result in 50.3 stands, scoped as it already was.

Section 6.4 carries a final-acceptance requirement: generate the simulator
source twice for each original branch -- once with **that branch's own
generator**, once on rebased master -- and require exact raw byte equality.

**No branch has the Direction-2 launcher** (the overbroad conclusion drawn from
this, corrected in section 51):

| branch | reference | `make_sim_launcher` | `Sims/` tree | any file mentioning `Sims/` |
|---|---|---|---|---|
| D3 | `78a3e0d8c` | no | no | none |
| D4 | `f6ad1fe29` | no | no | none |
| FP | `9bb41a09d` | no | no | none |

`pass/lean/scripts/make_sim_launcher.py` exists only on master, and its own
docstring says what it is: "Direction 2's executable is a GENERIC simulator
over `DesignCert`". Choosing it as *the* generator for 6.4 was an
implementation assumption of this plan, not a user requirement, and it was the
wrong artifact -- section 51 identifies the right one and the thirty checks are
**not** impossible. The three substitutions 6.4 forbids are still not offered
anywhere in this ledger.

### 50.3 What could be established instead, and was

Two things, both recorded in `pass/lean/tests/MASTER_SIM_SOURCE_FROZEN10.tsv`.

**Preservation, directly.** The D3/D4/FP port never touched the generator. Its
last change is `4ab3ac566` ("sim: run the ported launcher, and correct its
invocation"), **77 commits before** the port's first commit `73dee5673`. The
file is byte-unchanged across the entire port, which answers the preservation
question more directly than regenerating and diffing outputs would.

**Determinism, measured.** Ten blocks were frozen and written to
`generated/master_port/sim_frozen10.json` **before** anything was generated --
`debug_breakpoint`, `intpipe_alu`, `intpipe_csr_file_conv`,
`intpipe_csr_file_fl_barrier`, `intpipe_csr_msgs`,
`intpipe_csr_pmu_read_interface`, `intpipe_csr_replay`, `intpipe_decode`,
`intpipe_fp_scoreboard`, `intpipe_imm`. The generator then ran twice over
exactly those ten, the same inode hard-linked into two throwaway trees, same
toolchain, no flags. Both passes exit 0 and emit 20 files; `diff -r` over the
two `Sims` trees is **empty**, the two lakefile stanzas are identical, and the
two sha256 listings match line for line. Raw bytes, not normalised.

That is determinism of master's generator. It is **not** the 6.4 preservation
check and is not offered as one.

## 51. Correction: every branch has a simulator-source emitter, and it is not the launcher

### 51.1 What section 50.2 got wrong

Section 50.2 concluded that section 6.4 "cannot be executed as written" because
no original branch has a generator. That conclusion was wrong, and the error
was a **mapping** error, not a measurement error: the search looked only for
master's Direction-2 `make_sim_launcher.py` and its `Sims/` tree. Those really
are absent at all three pins -- but their absence says nothing about whether
those branches emit simulator source, because that is **not the route they
use**.

All three pins emit Lean simulator source from a native C++ emitter,
`pass/lean/pass_lean.cpp` -- the `pass.lean` Eprp pass -- writing
`<output_dir>/<base_name>_Lgraph.lean`, which is the **same artifact name
master emits**:

| branch | pin | emitter sha256 (first 16) | lines | `_comb`/`_next`/`_step` at |
|---|---|---|---|---|
| D3 | `78a3e0d8c` | `eea28b5a16a79697` | 3,783 | 2796 / 2830 / 2890 |
| D4 | `f6ad1fe29` | `cd41e7d92cc82d76` | 3,855 | 2868 / 2902 / 2962 |
| FP | `9bb41a09d` | `ff2345c55705f8d5` | 2,455 | 1718 / 1752 / 1812 |
| master | `b9515eaf8` | driver only | 103 | split across the `emit_legacy_*` / `emit_design_cert` / `emit_dcert` modules (1,636 lines) |

Master refactored the monolith into modules while keeping the artifact name --
which is precisely the kind of change a byte comparison exists to police.
The full map, with invocations and per-route status, is
`pass/lean/tests/MASTER_SIM_ARTIFACT_MAP.tsv`.

### 51.2 The routes, separated

They are different paths and are not substituted for one another.

- **Native `pass.lean` emitter** (all three pins and master). Input is RTL or
  an LGraph, not a certificate. The FP benchmark invocation is pinned in
  `scripts/run_cva6_module_lean_stress.sh`: `lhd compile verilog --reader
  yosys-slang --top <TOP> --emit-dir lean:<LEAN_DIR> --set
  formal.lean.strict=true --set formal.lean.normalize=true --set
  formal.lean.emit_cert=<EC> --set formal.lean.emit_fast_bridge=<EFB> --set
  formal.lean.cert_wf=<CW> --set formal.lean.max_width=1048576`.
- **Checked-compiler emission**, `pass.lean mode:verified_compiler`. Present at
  D3 (19 sites) and D4 (22 sites) and on master, where it publishes three
  artifacts in one transaction. **Absent at the FP pin**: FP's `pass_lean.cpp`
  has no `mode` option at all and zero occurrences of `verified_compiler`. That
  one comparison cannot exist for FP, established from the pinned
  implementation rather than from a missing directory.
- **FP specialized residual.** No FP probe writes the residual to a file; it is
  an in-memory `Program`. The digest, `szOf` and the decidable `R == F` test of
  section 48 are in-process observations of a `Program` on the **current**
  build. They are not generated-source bytes and are not offered as a
  substitute for any 6.4 comparison.
- **D3 reified residual.** Emits under a gitignored path; not inventoried at
  this checkpoint, recorded as open.

### 51.3 An option the rebase dropped

Section 6.4 step 3 requires identical options, which makes the option surface
load-bearing. All three pins accept `strict`, `emit_cert`, `emit_fast_bridge`,
`cert_wf`, `cert_wf_fallback`, `cert_chunk_size`, `cert_chunk_limit`,
`max_width`, `top` and **`normalize`**. D3 and D4 additionally accept `mode`.

**Master accepts the same set minus `normalize`.** The original FP benchmark
sets `formal.lean.normalize=true` explicitly, and master has no such knob --
only internal normalisation helpers in `design_scan.cpp`. Pinning what master's
behaviour corresponds to is a prerequisite for any byte comparison, and it is
recorded here as open rather than assumed away.

### 51.4 The one small comparison, and why it is a characterisation

113 block names already carry a `_Lgraph.lean` on **both** master and the D4
worktree, so one pair could be compared with no build at all. Taking
`ariane_regfile_gate` (retained in
`generated/master_port/sim_bytecmp/ariane_regfile_characterisation.txt`):

```
original  D4 temp/cva6_30_auth/.../ariane_regfile_gate_Lgraph.lean
          sha256 6d8f46eff208810a...  147,810 bytes
master    generated/master_port/d4_cva6_30/.../ariane_regfile_gate_Lgraph.lean
          sha256 9df9dd297112ad59...   69,104 bytes
byte-equal: NO
```

Both headers say `pass.lean in formal.lean.mode=verified_compiler`, confirming
the route. The difference has **two independent causes**, and only one of them
is what 6.4 tests:

1. **The inputs are different elaborations.** Master declares 400 sources
   (`0..399`), D4 declares 1,034 (`0..1033`) -- stable across all seven D4
   copies and all master copies. The same gap appears in the certificates
   (master `btb` 1,059 sources against D4's 1,784).
2. **The emitter changed.** Master emits 4 theorems where D4 emitted 2, and the
   header text differs: master's "the checks and theorems below instantiate its
   generic correctness and all-fire compatibility results" against D4's "This
   file contains NO semantic model and NO proof script".

Because of (1) this pair does **not** meet 6.4 step 3, so it is recorded as a
characterisation and **not** as a pass or a fail. Reporting it as a mismatch
would be reporting an uninterpretable one.

### 51.5 What each remaining byte check needs

The ten-block criterion stands, unchanged and not replaced: not by master-only
determinism, not by generic launcher output, not by behavioural agreement, not
by normalisation, and not by a new equivalence proof. The frozen samples
already recorded are kept and will be validated for applicability before
generation, never repicked after a mismatch.

To make the checks executable, per branch: build the pinned branch, re-emit the
ten blocks from **identical pinned RTL/filelist inputs** with the **same
emission options** (including whatever master's answer to `normalize` is),
re-emit the same ten on master through the corresponding route, and compare raw
bytes, retaining every input and output hash and the full diff of any mismatch.
That needs a heavy build on each branch and is **not started here**, because
the long FP benchmark runs own the machine at this checkpoint.

The verified-compiler example and its original emitter stay in the plan: D3 and
D4 carry `mode:verified_compiler`, master carries it, and that comparison is
listed as MAPPED/OPEN. For FP it is recorded as ABSENT AT THE PIN, narrowly,
with the implementation evidence.

## 52. The `normalize` option, audited to its implementation

### 52.1 What it is, and what it is not

Section 51.3 flagged `formal.lean.normalize` as an option the rebase dropped,
and recorded it as open rather than assumed away. Traced to its implementation
it turns out to be inert on the Lean route, and the real behaviour it names is
intact. Full table in `pass/lean/tests/MASTER_LEAN_NORMALIZE_AUDIT.tsv`.

**At all three pins the member is write-only.** It is declared
(`pass_lean.hpp:31`), parsed (D3 `:1948-49`, D4 `:1964-65`, FP `:1368-69`) and
registered (D3 `:2005`, D4 `:2021`, FP `:1416`) -- and **read nowhere**.
`if (normalize)` appears in no `pass/lean` file at any pin, and
`git log -S'if (normalize)' -- pass/lean` returns **no commit in master's
entire history**, so the Lean emitter never read it at any point.

**The behaviour the name refers to is real, and it is preserved.** It lives in
the Isabelle emitter:

```cpp
if (ctx.normalize) {
  normalize_zero_width_get_masks(ctx, topo);
}
```

master `pass/isabelle/pass_isabelle.cpp:2649` (bound at `:2489`), D4 pin at
`:2711` (bound at `:2553`). Master's kernel still routes `formal.normalize` to
that pass (`lhd_kernel_common.cpp:2614-15`).

So the answer is the middle one of the three the audit asked about: **a flag is
simply no longer exposed on the Lean route; no preprocessing behaviour was
removed from it, because none was ever behind it there.** No no-op option was
added to make the sets match, and no legacy normalization was restored.

### 52.2 Two consequences for the byte comparison

**The option sets reconcile without a change.** `check_labels`
(`pass/common/eprp_method.cpp:15-28`) enforces only *required* labels, so
passing `--set formal.lean.normalize=true` to master is accepted and ignored.
Since neither side ever read it, the emitted bytes cannot depend on it at
either end, and the original invocation can be replayed verbatim.

**The removal is upstream, not this port.** `b7a24e0b0`, Jose Renau,
2026-09-14, *"several cleanups and reviews on options and miss-used options"* --
295 commits back on master. It is present in the D3 and D4 repositories but is
**not an ancestor of their pins**, and is absent from the FP repository
entirely. The same commit dropped the unprefixed `formal.strict` /
`formal.normalize` fallbacks from `emit_lean_outputs`; the `formal.lean.*`
prefixed form, which is what the original benchmark uses, still reaches
`pass.lean`.

### 52.3 No identical-graph handoff exists, on any side

> **WRONG, corrected in section 53.2.** `emit_lean_outputs` really is called
> only from `lhd_kernel_compile.cpp`, but `lhd compile` takes an `lg:`
> directory as an INPUT as well as `verilog:`, so that call site is reachable
> from an LGraph. The pinned CORE-ET runner uses exactly that form. An
> identical-graph handoff IS available.

The manifest needed to say whether the frontend must be reproduced or whether
an LGraph could simply be handed over. **`emit_lean_outputs` is called from
`lhd_kernel_compile.cpp` and nowhere else** -- D3 `:1401`, D4 `:1401`, FP
`:1533`, master `:1955`. The inference drawn from that below does not follow.

The `ariane_regfile` pair of 51.4 still cannot be fixed by swapping inputs:
400 sources against 1,034 is a frontend difference between two unrelated
elaborations, and removing it needs either re-elaboration from the same pinned
RTL or the graph handoff of 53.2.

### 52.4 The paired-generation manifest

`pass/lean/tests/MASTER_SIM_PAIRED_GENERATION.tsv`. **Nothing has been
generated from it.** Applicability is recorded before generation and the
samples are not repicked.

- The CORE-ET frozen ten are **applicable to D3** (10/10 in
  `pass/lean/manifests/coreet_122.tsv`) and **to D4** (10/10 in
  `pass/lean/tests/d4/coreet_122.list`).
- They are **not applicable to FP**, whose pin carries CORE-ET only as a
  TSV-consuming census; its native emitter benchmark is CVA6. An FP ten was
  therefore frozen at this checkpoint, before any generation, by a rule written
  down first: `alu alu_wrapper commit_stage compressed_decoder decoder
  instr_scan pmp pmp_data_if ariane_regfile serdiv`
  (`generated/master_port/sim_frozen10_fp.json`).
- Pinned inputs today: CVA6 at `bc510437` (`v5.3.0-167-gbc510437`), working tree
  **clean**, target `cv64a6_imafdc_sv39_hpdcache_wb`, filelist via bender
  0.32.0; CORE-ET at `c72b6a2` with **one dirty file**, recorded as a blocker
  rather than waved through.
- `bazel-bin/lhd/lhd` exists only on master (2026-10-10, sha256
  `414d5dcbf99b37fe...`). It is **missing at all three branch worktrees**, and
  the build target is `//lhd:lhd` on every side (D3 `BUILD:106`, D4 `:106`, FP
  `:146`). The command recorded for later is
  `nice -n19 ionice -c3 bazel --output_user_root=<isolated> build -c opt //lhd:lhd`
  in each worktree, after verifying its HEAD is the pin. **Not run here** --
  the long FP benchmark runs own the machine.

### 52.5 `issue_read_operands` at 7200 s, and a projection I should not have made

The retry terminated at its cap: **exit 124, wall 7200.18 s, four agreeing
seeds (0, 1, 2, 3)**, `oom_kill 0`, peak 112 MB of the 8 GiB. It remains
**REFUSED / undecided**, not a pass, and the 1800 s and 7200 s attempts are
retained separately.

My projection of ~5,600 s for it was unsound, and the reason is worth
recording. I extrapolated a constant per-seed cost from `[ref 0]`. The
reference-run cost is in fact strongly **stimulus-dependent**:

| seed | 0 | 1 | 2 | 3 |
|---|---|---|---|---|
| `[ref]` ms | 507,924 | **1,788,680** | 402,073 | **3,067,588** |
| `[step]` ms | 2,556 | 14,304 | 3,408 | 14,129 |

a 7.6x spread on the reference and 5.6x on the residual. Any projection from a
single seed is therefore worthless, and the 5-19 h figures I gave for
`scoreboard`, `frontend`, `issue_stage` and `csr_regfile` carry the same defect
and should be read as lower bounds only. What the table does show, at every
seed, is the residual holding a 118x-217x margin over the interpreter.

`bht`'s 7200 s attempt continues; the remaining long entries stay open, and a
resource stop never counts as coverage.

## 53. The input freeze, and two corrections to section 52

### 53.1 core-et is not dirty

Section 52.4 listed "CORE-ET at `c72b6a2` with one dirty file" as a blocker.
That was wrong. `git diff --name-only HEAD` on that repository is **empty**;
the tracked tree is clean. The single status entry is `?? core_et_blocks.txt`,
an **untracked** 16,674-byte text inventory dated 2026-08-19, and no runner in
this pipeline consumes it -- the only references anywhere are
`coreet_preflight.sh`, its controls and a README, none of them on the filelist
path. It is not an input, and it was left exactly where it is: not deleted, not
stashed, not cleaned, and no permission asked to remove something that is not
in the way.

### 53.2 An identical-graph handoff does exist

Section 52.3 concluded from `emit_lean_outputs` having one call site that no
route loads an LGraph and emits Lean. The call site is right; the inference is
not. **`lhd compile` takes `lg:<dir>` as an input form**, not only `verilog:`
(`lhd_kernel_compile.cpp:133`, "an `lg:`-only compile (no sources, no `ln:`
inputs) has no LNAST on the pipe"), and that path reaches `emit_lean_outputs`
normally.

The pinned CORE-ET runner uses exactly that form. It is a **three-step**
pipeline, not one:

```
1) lhd compile verilog $ANCHOR --reader yosys-slang --top $TOP \
     --emit-dir lg:$LG_RAW --set yosys.filelist_file=$FILELIST \
     --set yosys.setundef=zero -- --ignore-assertions --relax-enum-conversions
2) lhd pass single_edge --top $TOP lg:$LG_RAW --emit-dir lg:$LG_NORM
3) lhd compile lg:$LG_NORM --top $TOP --emit-dir lean:$LEAN_DIR \
     --set formal.lean.strict=true --set formal.lean.normalize=true \
     --set formal.lean.emit_cert=$EMIT_CERT --set formal.lean.emit_fast_bridge=$EMIT_FAST_BRIDGE
```

Step 2 is mandatory, not incidental: `pass.lean` fatals on `Ntype_op::Latch`
and core-et's `tech_generic` primitives are two-phase latch cells. A
mandatory RTL-vs-LGraph LEC gate runs beside it.

So two experiments are available, and they answer different questions.
**(A) Identical external inputs**: run all three steps on both sides from the
same filelist bytes, pin, reader and options. **(B) Identical graph handoff**:
run steps 1-2 once and step 3 on both sides against the same `$LG_NORM`,
isolating the emitter from the frontend. (B) has an unverified prerequisite --
that the on-disk GraphLibrary one binary writes is readable by the other -- and
that is recorded as a question, not assumed.

**The comparison's boundary.** Under identical external inputs, a difference in
the emitted source is a **real pipeline byte mismatch** and is retained as one,
attributed to whatever stage the evidence supports. It is not discarded because
the intermediate graph shapes differ. Pre-existing files from unrelated
elaborations, like the `ariane_regfile` pair of 51.4, stay characterisations.
The check is exact raw generated source, headers and theorem text included --
no normalisation, no launcher substitution, no behavioural-equivalence proof,
no master-only determinism.

### 53.3 What is now frozen, by content

`pass/lean/tests/MASTER_SIM_COREET10_INPUTS.tsv`, with the full hash set in
`generated/master_port/sim_inputs/coreet10_inputs.json`. **Nothing has been
emitted from it.**

A repository HEAD does not pin these inputs, so a project-local snapshot does.
`coreet_filelist.sh` reads `<repo>/generated/core-et/patched_src/map.tsv` at
**runtime** and substitutes declaration-reorder copies for **four** source
files -- the script's own comment says "two", and the map has four rows. That
path is gitignored, so the map and all four files are frozen by content in
`generated/master_port/sim_inputs/pinned/`. The four patched files are
**byte-identical** between the D4 repo and master; only `map.tsv` differs, and
only in the absolute destination paths it embeds -- which is notable because
the three repos carry **three different** `coreet_patch_srcs.py` generators
(D3 `09819356a420`, D4 `2cce04803dab`, master `3ecca755d659`). D3's repo has no
`map.tsv` at all, a real missing prerequisite recorded as such.

The filelists were then generated read-only -- `coreet_print.mk` is
`print-%: ; @echo '$($*)'` and an explicit goal runs no recipe -- with GNU Make
4.4.1, `TECH=generic`, and `TMPDIR`/`TMP`/`TEMP` pointed at a project-local
directory because the script uses `mktemp`:

| block | src files | origin | incdir | patched |
|---|---|---|---|---|
| debug_breakpoint | 6 | `dv/rtlcosim/.../Makefile:NEW_RTL_FINAL` | no | 0 |
| intpipe_alu | 6 | same | no | 0 |
| intpipe_csr_file_conv | 6 | same | yes | 0 |
| intpipe_csr_file_fl_barrier | 6 | same | yes | 0 |
| intpipe_csr_msgs | 6 | same | yes | 0 |
| intpipe_csr_pmu_read_interface | 7 | same | yes | 0 |
| intpipe_csr_replay | 6 | same | yes | 0 |
| **intpipe_decode** | **156** | `hw/ip/minion/dv/Makefile:RTL_SRCS` | yes | **4** |
| intpipe_fp_scoreboard | 7 | same as the rest | no | 0 |
| intpipe_imm | 6 | same | no | 0 |

**156 unique files** are consumed across the ten, every one verified readable
(0 missing), each with its full sha256 retained. Order is dependency order and
is preserved. Only `intpipe_decode` takes the whole-IP fallback, and it is the
only block that pulls in the four patched sources. Six blocks carry
`+incdir+.../intpipe/csr/rtl`, whose eight `.svh` headers are hashed as inputs
too.

### 53.4 Corrected applicability, and what is still missing

FP's CORE-ET applicability is relabelled. Section 52.4 said the CORE-ET ten are
"not applicable to FP". The evidence only showed that FP's pin carries CORE-ET
as a TSV-consuming census; that does **not** establish that FP's native emitter
cannot generate CORE-ET blocks, since it is the same `pass.lean` route. The
honest label is **untested**. The FP ten frozen at the previous checkpoint
stays frozen, and emission applicability for every row stays untested until a
run establishes it.

Still missing, and not started here: `bazel-bin/lhd/lhd` at all three branch
worktrees (`//lhd:lhd`, command recorded); the per-block `$ANCHOR` and `$REF_SV`
values the runner derives; the yosys and yosys-slang versions on each side; and
D3's absent `patched_src/map.tsv`.

`bht`'s 7200 s attempt continues. No build, no emission batch, no cache
mutation in this checkpoint.

## 54. The input freeze completed, and the one pin that is not shared

### 54.1 D3's missing map is not a blocker

Section 53.3 called D3's absent `patched_src/map.tsv` "a real missing
prerequisite". It changes bytes, it does not stop the script. `subst_of()`
opens with

```bash
[[ -r "$PATCH_MAP" ]] || { printf '%s' "$1"; return; }
```

so an absent map falls back to the **original** source path. Verified by
running the pinned script from a snapshot with no map: **rc=0**, the four
originals named instead of the patched copies, and the `// patched:` notes
gone. Both variants of the only affected block are frozen -- mapped
`f2d55030f8ce...`, unmapped `187f671ffdd6...` -- and the other nine have zero
substitutions, so their variants coincide.

The controlled paired experiment uses the frozen **mapped** filelist on both
sides, so the two sides see identical bytes. D3's own in-place historical
route, which would use the originals, is kept as separate provenance and is not
silently swapped for the mapped one.

### 54.2 The headers belong to one block, not six

Section 53.3 said six blocks carry the `+incdir+` "whose eight `.svh` headers
are inputs too". Scanning every consumed file for `` `include `` rather than
trusting directory membership: there are exactly **eight** `` `include ``
directives across all 156 files, **all eight in one file**,
`intpipe_csr_file.sv`, and that file is named by exactly **one** of the ten
blocks -- `intpipe_decode`. The other five incdir-carrying blocks get it
because `coreet_filelist.sh` tests `grep -q '/intpipe/csr/rtl/'`, which matches
any file from that directory (`intpipe_csr_file_conv.sv`,
`intpipe_csr_msgs.sv`). They receive an incdir they never use. The eight
headers are now in the machine-readable manifest with full sha256, bound to
`intpipe_decode` alone.

### 54.3 The runner's generated inputs are derived, not deferred

Both are deterministic functions of bytes already frozen, so no tool was run to
obtain them and none had to be.

**ANCHOR** is one `printf` of a single comment line: 70 bytes, sha256
`22857e1702a1ed2f...`, identical for every CORE-ET block. The CVA6 runner's
equivalent is 82 bytes, `60d722902e8811e2...`.

**REF_SV** is `cat $(grep '\.sv$' $FILELIST)` in filelist order -- the LEC
gate's reference side. Every block's byte count and sha256 is recorded, from
`intpipe_imm` at 86,353 bytes to `intpipe_decode` at 2,413,587. The four
originals behind the patched copies are hashed as well.

### 54.4 The FP ten's CVA6 inputs

`pass/lean/tests/MASTER_SIM_CVA6FP10_INPUTS.tsv`. The sample is unchanged.
Filelists produced by the pinned recipe, `bender script flist-plus -t
cv64a6_imafdc_sv39_hpdcache_wb --top <block> --trim-incdirs auto`, bender
0.32.0: ten filelists, **19 unique consumed files, 0 missing**, each with its
full sha256, plus the two `+define+` lines every list carries, the per-block
incdirs, the target config package and both Bender manifests. `Bender.lock` is
untracked and gitignored, so it is frozen by content.

Running the ten resolutions left `Bender.lock` **byte-identical** and the CVA6
checkout at **0 status entries** -- the user's tree was not mutated. This route
has no LEC gate and no `REF_SV`, and it is a one-step pipeline, unlike
CORE-ET's three.

### 54.5 slang is shared; yosys is not

Resolved from `MODULE.bazel` on each side, source only:

| | D3 | D4 | FP | master |
|---|---|---|---|---|
| slang | `f33547e9…` slang-11.0 | same | same | **same** |
| yosys | `cd2b29bb…` yosys-0.60 | same | same | **`69fbe16a…` yosys-435977e9 (a git snapshot)** |

`--reader yosys-slang` runs the **vendored** yosys, so under identical external
inputs the two sides do **not** share a front-end binary. This is declared
before any run rather than discovered after one. It does not weaken the check:
a resulting raw-source difference is a **real pipeline byte mismatch**, retained
and attributed to whatever stage the evidence supports. It does mean attribution
will often land on the reader rather than on `pass.lean` -- which is exactly why
the identical-`$LG_NORM` handoff of 53.2 is worth running beside it as a
**diagnostic**: step 3 never invokes yosys, so it isolates the emitter. The
handoff supplements the same-external-input checks and cannot waive a mismatch
they find. Its own prerequisite -- that one binary's on-disk GraphLibrary is
readable by the other -- is still unverified.

The system `slang` on PATH (9.1.214) is not the one used, and `yosys` is not on
PATH at all; `inou_yosys_api.cpp` execs the runfiles copy. D3 and D4 share a
`MODULE.bazel` byte for byte; FP's and master's each differ.

### 54.6 Remaining execution prerequisites

`pass/lean/tests/MASTER_SIM_TOOLCHAIN.tsv` carries the executable commands --
isolated bazel output roots, real paths, explicit options, project-local
`TMPDIR`. Nothing in it was run. What is still needed:

1. `bazel-bin/lhd/lhd` at the three branch worktrees (`//lhd:lhd`), each built
   into its own output root after verifying HEAD is the pin. Present on master
   only.
2. Verification that the `lg:` GraphLibrary written by one binary is readable
   by the other, before the handoff diagnostic can mean anything.
3. The `$LG_RAW` / `$LG_NORM` / emitted `_Lgraph.lean` artifacts themselves,
   which are **deferred artifacts**: they need a real generation run, and no
   content hash is invented for them here.

`bht`'s 7200 s attempt continues. The remaining long FP blocks --
`issue_read_operands` (capped twice, undecided), `scoreboard`, `frontend`,
`issue_stage`, `csr_regfile` -- stay explicit in the queue. A cap stop or a run
of agreeing partial seeds does not close a block.

## 55. Generation latency as its own metric, and a candidate regression

The priority changed: **simulator generation latency is a standalone metric,
ahead of reference validation.** `total_probe` and its admission contract are
untouched; a second, generation-only driver sits beside it.

### 55.1 The driver

`gen_probe` stops after load, support, specialize and fragment-check, and
measures end-to-end generation as exactly those four. It never runs reference
stimuli, the trace or the control interpreter, and its success is
**`GENERATION OK`** -- weaker than a benchmark pass and never entered as one.
Full procedure, proof accounting and build details:
`pass/lean/tests/fp/fp_generation_only.md`.

The artifact is an **in-memory `Program`**: nothing is serialized, no source is
written, and it is not raw native-emitter source.

### 55.2 Every historical "specialize" number includes a szOf traversal

> **MEASURED AND REFUTED in section 56.** `szOf` on ALU's 89,996-term residual
> takes **0 ms**. The digest really is inside `stage`'s timed region, but it
> costs nothing at these sizes, so it explains none of the gap. The hypothesis
> below was wrong.

`total_probe` times its stages with `Projection.Runner.stage`, which calls its
digest function **inside** the timed region:

```lean
let t0 ← IO.monoMsNow
let v ← IO.lazyPure thunk
let d ← IO.lazyPure (fun _ => digest v)   -- szOf: a full traversal
let t1 ← IO.monoMsNow
```

So every `specialize` and `specialize_and_check` figure in the manifests --
including master's 18.917 s and 19.423 s for ALU -- includes a complete `szOf`
walk of the residual. `gen_probe` moves that traversal **after** the measured
interval and reports it separately. That alone may account for a large part of
the gap against a remembered sub-10-second generation, and it is measured
rather than assumed.

### 55.3 The matched-ALU history, with the residual held constant

> **SUPERSEDED by section 56.** The pin generates ALU in 18.53 s, so the +35%
> against S2 is **not** a port regression. Every file on the generation path is
> byte-identical between S2 and the pin, so it is not a code regression at all:
> S2's 14.397 s and today's 18.53 s are the same code on different machine
> state. The real port delta is +5.2% on ALU.

Same certificate `d731ee22a85e77ba…`, same runner, same fuel 200000/2000, same
`Projection.mixDriver`:

| revision | stage | spec+check ms | residual terms | bound |
|---|---|---|---|---|
| `ecf59cf7d` | pre-S1 | 61,267,341 | 89,996 | 28,899 |
| `8d7e9225a` | S1 | 162,634 | 89,996 | 28,899 |
| `3c175a4da` | **S2** | **14,397** | 89,996 | 28,899 |
| `8d220f3fd` | **master replay** | **19,423** | 89,996 | 28,899 |

and `alu_wrapper` (`209e122f54ef…`) independently: S2 **14,139** ms, master
**19,118** ms, 89,961 terms recorded in both.

**Term count and checker bound are the same at every revision** (89,996 and
28,899). That is consistent with an identical residual but does **not establish
one**: two different programs can share a term count and a bound. No structural
comparison was made across revisions, so no identity is claimed. So the +34.9% on `alu` and +35.2% on `alu_wrapper` cannot be explained by
residual size, and the two designs agree to within 0.3 percentage points, which
is not noise. That is a **candidate reproducible regression**, and it is only a
candidate: `3c175a4da` is **S2**, not the FP pin. The controlled comparison is
the pin `9bb41a09d` (S3) against master, which is what the matched build will
decide. Master's five specializer files are byte-identical to that pin, so if
the pin also measures ~19.4 s the cost is inherited rather than introduced; if
it measures ~14.4 s the port introduced it despite identical specializer bytes.

The user's remembered sub-10-second generation is not matched by any retained
row: S2's 14.397 s is the fastest ALU figure in either manifest, and it includes
the `szOf` traversal of 55.2. Nothing here is claimed to satisfy that
expectation; the hypothesis to test is that generation without the traversal is
materially below the recorded figure.

### 55.4 Rows frozen before running

33 rows, frozen to `generated/master_port/fp/genprobe/frozen_rows.json` **before
any generation run** and not to be repicked: **all 30** CVA6-30 frozen names,
support or no support, plus the three `rt_*` designs of FP's original CVA6
evidence. Every one resolves to a certificate (29 in `cva6_30_auth`, 1 in
`cva6_30_auth2`, 3 in `livehd-d4-incremental/temp`), each pinned by full
sha256. `alu`'s is `d731ee22a85e77ba…`, the certificate of the historical row.

Unsupported operator/memory cases, admission refusals, fuel stops and generation
failures are recorded **as rows**, not dropped from the denominator.
`pmp_data_if`'s historical DCERT1 generation stays distinct from its fresh
`EMIT_FAILED rc=6`. CORE-ET and DINO are execution-applicability work and are
not borrowed as FP coverage, and no D3 or D4 coverage is substituted for FP.

### 55.5 Where generation is genuinely slow

Independently of any reference validation, the rebased generation stage already
measured, before fragment checking:

| block | specialize ms |
|---|---|
| `issue_read_operands` | 21,323 |
| `bht` | 35,118 |
| `scoreboard` | 52,139 |
| `frontend` | 113,527 |
| `issue_stage` | 135,352 |
| **`csr_regfile`** | **722,358** |

Twelve minutes for `csr_regfile` is a generation cost, not a validation cost.
These six are the profiling targets, and the rule is that a cause must be
demonstrated rather than inferred from residual size, and that a demonstrated
regression gets a bounded repair with a retained before/after reproducer --
not a raised timeout.

### 55.6 Order of work

`bht`'s reference-validation run owns the machine and is being left to finish;
its evidence is preserved, and its measured phases stay separate metrics --
specialize 35.118 s, check 0.229 s, six reference stimuli 3,235.428 s, six
residual steps 12.819 s, with the trace and control uninstrumented and still
pending. None of that elapsed time is generation latency. Builds and the serial
matched timing follow on a quiet machine. No second long reference-validation
job is launched ahead of this.

## Final stage requested by the user: 12 × 5 simulator generation matrix

Execute after the existing plan and acceptance obligations. [Plan section 15](PASS_LEAN_RESTRUCTURE_PLAN.md#15-final-stage-simulator-generation-across-five-approaches-and-twelve-blockscores) and the [live progress table](../../todo/livehd/simulator-generation-matrix.html) define all twelve rows and five approach columns. Prioritize standalone generation latency and complete block coverage. Every cell is PENDING until its own generation evidence exists. Only when all 60 cells are YES, make the final checkpoint and commit locally; the user will push. PicoRV32 uses `/HOMEROOT/projects/picorv32`. Current generation-first experiments continue unchanged.

## 56. Generation latency, measured matched against the exact pin

Both drivers built and run: pin `9bb41a09d` (binary `eeada8cf4405dc36…`) and
master (`b7abe8e056b589cb…`), both Lean 4.31.0 at the same commit, same `leanc`
clang 22.1.4, `-O2`, fuel 200000/2000, identical certificate bytes. Runs are
interleaved pin/master back to back with the load average recorded beside each;
the box carries a steady ~34 background load from other users that cannot be
removed, so the design controls for it rather than claiming an idle machine.
Table: `pass/lean/tests/fp/fp_generation_latency.tsv`.

### 56.1 Coverage over the 33 frozen rows

**25 `GENERATION OK`, 8 `GENERATION REFUSED (unsupported)`, 0 generation
failures, 0 fuel stops, 0 missing inputs.** The eight refusals -- `aes`,
`cva6_hpdcache_subsystem`, `cva6_hpdcache_wrapper`, `fpu_wrap`, `load_unit`,
`mult`, `multiplier`, `store_unit` -- stay **in** the denominator and refuse
cheaply, 8 to 120 ms. `GENERATION OK` is not a benchmark pass: no stimuli, no
trace, no control.

### 56.2 Sub-10-second generation exists, and ALU is not one of those blocks

Master, ascending:

| block | ms | | block | ms |
|---|---|---|---|---|
| commit_stage | 638 | | alu | 19,493 |
| ariane_regfile | 642 | | rt_alu | 19,543 |
| serdiv | 676 | | issue_read_operands | 20,883 |
| instr_scan | 754 | | bht | 33,555 |
| instr_queue | 956 | | decoder | 34,019 |
| store_buffer | 1,242 | | rt_decoder | 34,218 |
| perf_counters | 1,398 | | scoreboard | 55,624 |
| compressed_decoder | 1,692 | | id_stage | 56,736 |
| btb | 1,797 | | frontend | 118,149 |
| pmp | 4,243 | | issue_stage | 134,651 |
| cva6_ptw | 6,170 | | rt_csr_regfile | 663,828 |
| pmp_data_if | 8,274 | | csr_regfile | 684,535 |
| alu_wrapper | 19,232 | | | |

**5 blocks generate in under a second, 12 of 25 under ten seconds, 21 under a
minute.** So the remembered sub-10-second generation is real and reproduced --
for most of the inventory. ALU is simply not one of those blocks: at 19.5 s it
is in the slowest third, and no retained row ever put it below 14.4 s.

### 56.3 The expensive generation is inherited, not introduced

| block | pin ms | master ms | delta |
|---|---|---|---|
| issue_read_operands | 20,376 | 20,883 | +2.5% |
| bht | 32,249 | 33,555 | +4.1% |
| scoreboard | 50,428 | 55,624 | +10.3% |
| frontend | 117,467 | 118,149 | +0.6% |
| issue_stage | 130,191 | 134,651 | +3.4% |
| **csr_regfile** | **663,170** | **684,535** | +3.2% |

`csr_regfile`'s eleven minutes exist **at the FP pin itself**. The port did not
create the slow blocks; it inherited them. That is the answer to where
generation is genuinely slow: in the original FP specializer.

### 56.4 The port costs a uniform few percent, and the phase is specialization

Across the 25 `GENERATION OK` rows, master is slower in **25 of 25** -- minimum
+0.6%, **median +5.8%**, maximum +10.3%, and +3.0% on the summed specialization
time.

> **NARROWED IN SECTION 57.** Every one of those pairs ran **pin first, master
> second, unpinned**, so the unanimity cannot by itself exclude an order or
> warm-up effect. Under a counterbalanced A-B/B-A design with shared CPU
> affinity the sign holds on two blocks (+6.4% and +6.8%) and **reverses on
> `alu`** (-22.9%). The delta is real on the clean case and is **not** the
> uniform slowdown this paragraph suggests.

The phase breakdown locates it exactly. Per block, pin against master:

* **load** -- a few ms both sides. The `Compiler.CertIO` to
  `Compiler.CertIOV1` adaptation is **exonerated**.
* **support** -- 0 to 6 ms both sides. Exonerated.
* **check** -- indistinguishable (`4/4`, `785/771`, `1141/1138`). Exonerated.
* **specialize-whnf** -- master slower in **25 of 25**, and it carries the whole
  delta.

`specialize-force` is 0 everywhere: `mixDriver` is strict, so the entire
residual is built inside the WHNF force and nothing is deferred.

**The cause is not isolated.** Of the 38 modules in the build, exactly four
differ between pin and master -- `Translation/LGraphModel`, `Compiler/CertIO`
vs `CertIOV1`, `Projection/OperatorBridge`, `Projection/CertLoad` -- and every
specializer module (`PartialEvaluator`, `PartialEvaluatorCorrect`, `MixProgram`,
`PartialEvaluatorSummary`, `HardwareInterpreter`, `ResidualFragment`,
`VariantExec`, `RunnerSupport`) is **byte-identical**. The only hot-path
difference is `LGraphModel`'s `bv_shl_step`, where master adds a guard
(`if (bv_uint b).toNat ≥ w then 0 else …`) and changes `bv_sra` to saturate at
the source width -- the SHL/SRA boundary repairs, which are deliberate and are
not to be reverted.

**That attribution was tested and does not hold.** Shift density across the
seven profiled blocks ranges 14.3% to 32.6% while the delta ranges +0.6% to
+10.3%, with **Spearman rho = 0.07**; `ariane_regfile` at 7% shift density shows
a larger percentage delta than `alu` at 30%. What the data looks like instead is
a roughly **uniform multiplicative slowdown** of the specializer -- consistent
with compilation or code-layout differences between two binaries that differ in
four modules -- and that is recorded as unattributed rather than pinned on an
operator. No repair is proposed on an unestablished cause, and no timeout was
raised.

### 56.5 Two earlier hypotheses, refuted by measurement

**`szOf` costs 0 ms.** The digest really is inside `stage`'s timed region, but
on ALU's 89,996-term residual it takes under a millisecond. It explains nothing,
and every historical `specialize` figure was an honest measurement.

**S2 against the pin is not a regression.** Every file on the generation path is
byte-identical between S2 (`3c175a4da`) and the pin (`9bb41a09d`). Identical
code measured 14.397 s then and 18.53 s now, so that gap is machine state, not
the port and not the branch. S2's row is not a controlled baseline and is not
used as one.

### 56.6 Proof accounting, unchanged by any of this

Generation checks support (`supported_of_report` turns the observed Boolean into
`SupportedByProjection`) and observes `checkResidual R = some b`. It **trusts**
`hproj`: `mixDriver` does not kernel-reduce at nontrivial fuel, so no residual
here carries kernel-checked `hproj` evidence. The runtime-shape premises are
never reached. Nothing is discharged, nothing is kernel-certified, and no proof
flag is claimed.

## User priority update: coverage matrix before optimization

Run the user-requested twelve-block/core by five-approach generation table before further optimization and performance research. Keep the stage documented in plan section15 and preserve all other acceptance obligations. Record every cell's generation time, memory, CPU, phases and artifact/evidence identities, bound to the shared hardware/software dependency record in `tests/MASTER_SIM_GENERATION_ENVIRONMENT.json`, with contemporaneous per-attempt conditions. The initial environment snapshot describes current observations, not historical identity or an idle-machine guarantee. Use the completed coverage/metric table to choose subsequent optimization targets.

## 57. The queue bug, the counterbalanced result, and the first bridge lemma

### 57.1 A claim I made that was false

I reported the counterbalanced experiment as running. It was not. I had queued
it behind the profile run with

```bash
while pgrep -f "root-master/nat/gen_probe" >/dev/null; do sleep 20; done; bash …/counterbalanced.sh cb3 …
```

and that `pgrep -f` pattern **matches the waiter's own command line**, so the
guard never became false and the wrapper waited for itself. `runs/cb3` did not
exist, no `gen_probe` process existed, and the CSR profile had already
finished. The wrapper (pid 485264, started 17:03:22) was confirmed by argv and
start time, its provenance retained in
`runs/cb3_stale_queue_wrapper.log`, and only that pid was stopped. The defect
is written into `pass/lean/scripts/fp_gen_counterbalanced.sh` so it is not
repeated: wait on a real child pid or an exact executable, never on a substring
that appears in your own argv.

### 57.2 Counterbalanced result: the delta is real, and it is not uniform

8 paired repetitions per block, repetition 1 a retained warm-up excluded from
the summaries, alternating A-B/B-A, both sides pinned to **cpu80**, wall and CPU
time recorded, and the cgroup guard sampled per run -- `memory.max
8589934592`, exactly the requested 8 GiB, genuinely read back from the scope.

> **SCOPE CORRECTION.** The sampler reads the scope **once, at startup**, then
> breaks. So `memory.peak` and `memory.events` in the `cb3` evidence are
> **startup observations, not whole-run values** -- which is why sub-MB peaks
> sit beside ~21 MB `maxrss` in the same rows. Only `memory.max` is a genuine
> enforcement reading; the peak and the `oom`/`oom_kill` counters are initial,
> not final. The coverage runner samples until exit and keeps the final values.

| block | pin median | spread | master median | spread | delta | paired |
|---|---|---|---|---|---|---|
| `ariane_regfile` | 1,409 ms | **1.3%** | 1,499 ms | **0.3%** | **+6.4%** | master slower 7 of 7 |
| `alu_wrapper` | 37,353 ms | 8.1% | 39,895 ms | 16.3% | +6.8% | master slower 5 of 7 |
| `alu` | 32,270 ms | 41.2% | 24,883 ms | 40.1% | **-22.9%** | master slower 1 of 7 |

`ariane_regfile` is the clean case and it is decisive on its own terms: the two
distributions do **not overlap** (pin max 1,422 against master min 1,498) with
spreads under 1.5%, 7 of 7 paired. **`alu` reverses**, with 40% spreads on both
sides. Position does not explain the sign: pin's 1st- and 2nd-position medians
agree within a few percent, and so do master's.

So the honest statement is narrower than section 56.4's. A reproducible master
slowdown of about **6%** exists on the low-noise block and agrees in sign on a
second. The earlier "slower in 25 of 25" was one unpinned, pin-first pass, and
**ALU's sign is not stable** -- pinned to a shared physical core it flips. The
absolute numbers here are higher than the unpinned sweep because cpu80's SMT
sibling is cpu32 and other users' work lands there; that is recorded in
`runs/cb3/CPU_CONTEXT.txt` and makes the comparison fair rather than the
magnitudes comparable across configurations.

### 57.3 The bottleneck, as the evidence supports it

`perf` is unusable on this box (`kernel.perf_event_paranoid = 3`) and
valgrind/callgrind are absent, so sampling is by gdb batch attach. **Limits,
stated rather than buried:** each sample SIGSTOPs the process, so a profiled run
is slower than a production one and its duration is never quoted as a timing;
the rate is whatever the attach allows, not a fixed frequency; and `-O2` without
frame pointers can elide frames.

| profile | worker samples | `shiftGo` top | `shiftGo` anywhere | allocator top |
|---|---|---|---|---|
| `alu`, master | 25 | 12 (48.0%) | 19 (76.0%) | 11 (44.0%) |
| `alu`, pin | 25 | 11 (44.0%) | 17 (68.0%) | 11 (44.0%) |
| `csr_regfile`, master | 60 | 29 (48.3%) | 47 (78.3%) | 25 (41.6%) |

These are **evidence for a common dominant shift-and-allocation bottleneck** on
both branches and at both scales. 110 sparse samples do **not** establish that
the full hot paths are statistically identical, and they do not rule out an
execution-path difference behind the port delta. Likewise the near-zero
correlation between static shift density and the measured deltas does not show
the SHL/SRA guards cost nothing, and identical inspected source bytes between S2
and the pin do not establish machine state as the sole cause of that gap. All
three are recorded as unresolved. The SHL result-width, SRA source-width and
strict-greater Sext repairs stay exactly as they are.

### 57.4 `csr_regfile`, diagnosed on its own

Eleven minutes of generation -- 663 s at the pin, 685 s on master -- is not
acceptable merely because it is inherited. Its profile is the same shape as
ALU's at 36x the size: `shiftGo` on 78.3% of stacks, allocator traffic on most
of the rest. `csr_regfile` is 34,897 nodes with 8,077 `SHL`, and eager shifting
is quadratic in the way the pin's own S3 commit message describes --
*"deferred shifting removes the shift quadratic; the walk is now the cost"*.
The cost is therefore the same defect, not a separate one, and the same bridge
unblocks both.

### 57.5 What `ProtoShift` would need, precisely

`Proto/PartialEvaluatorShift.lean` is a **candidate**, not a fix. It carries
**zero theorems** and is not imported by `Audit.lean`. It cannot be promoted
into the covered path, because `specializeDesign_varT_correct` and
`simSound_varT` take `hproj : mixDriver sf wf hwAPVarT [encDesign D] = .ok R`
about the **proved** driver, and a run of an unproved fork does not produce that
premise.

The bridge it needs, concretely:

1. **A representation function** `⟦·⟧ : ProtoShift.PVal → PVal` that forces
   pending offsets, with `⟦shifted k v⟧ = PVal.shift k ⟦v⟧`, and the key lemma
   `⟦ProtoShift.PVal.shift k v⟧ = PVal.shift k ⟦v⟧` -- offset composition
   agreeing with eager shifting. **This rests on shift additivity, which did not
   exist.**
2. **Binder-offset discipline**: every site entering a binder adds the same `k`,
   so `shift` commutes with the environment extension `mix` performs.
3. **No variable capture**: `dyn i` under an offset becomes `dyn (i + k)`, and
   offsets apply only to values whose indices are in scope.
4. **Reification**: `toCodeAt` applies pending offsets during the traversal it
   already performs, and must agree with `toCodeAt ∘ ⟦·⟧`.
5. **The successful-specialization contract**:
   `ProtoShift.mixDriver sf wf I args = .ok R → mixDriver sf wf I args = .ok R`,
   without which `hproj` cannot be discharged from a fast run and none of the
   covered-backend theorems transfer.

### 57.6 One bounded lemma, kernel-checked

`Projection/Proto/ShiftBridge.lean` is new and proves the arithmetic content of
step 1 against the **proved** `PVal.shift` and `PEnv.shiftBy`:

```lean
theorem PVal_shift_add (k₁ k₂ : Nat) (v : PVal) :
    PVal.shift k₁ (PVal.shift k₂ v) = PVal.shift (k₁ + k₂) v
theorem PEnv_shiftBy_add (k₁ k₂ : Nat) (e : PEnv) :
    PEnv.shiftBy k₁ (PEnv.shiftBy k₂ e) = PEnv.shiftBy (k₁ + k₂) e
theorem PVal_shift_add_zero (k : Nat) (v : PVal) :
    PVal.shift k (PVal.shift 0 v) = PVal.shift k v
```

`#print axioms` on all three: **"does not depend on any axioms"** -- not
`sorryAx`, not `propext`, not `Classical.choice`, and no `native_decide`. This
is the law that lets `shifted k₁ (shifted k₂ v)` collapse to
`shifted (k₁ + k₂) v`; the existing file had `shift_zero`, `shiftGo_eq` and the
`@[csimp]` `k = 0` fast path, but **no additivity**.

It claims nothing further. It is not a representation function, it does not
relate `ProtoShift` to `PartialEvaluator`, and it says nothing about the
specializer being correct. Steps 2 to 5 remain open, and until step 5 exists the
covered backend keeps the eager shift and generation stays as slow as measured.
The pinned files are untouched -- this is a new module, so the five specializer
files are still byte-identical to the FP pin.

### 57.7 Still open

ALU under ten seconds is **unmet**: 19.5 s unpinned, and nothing in this
checkpoint moved it. `csr_regfile`'s eleven minutes are **unmet**. Identifying a
prototype does not close either. Coverage stands at 25 `GENERATION OK` and 8
unsupported refusals over the 33 frozen rows, with the eight kept in the
denominator, and the historical DCERT1 successes stay distinct from fresh DCERT2
support and export failures.

## 58. Coverage first: the 12x5 matrix starts running (PROVISIONAL)

Priority changed again: the coverage table runs **before** optimization and
performance research. The 48 counterbalanced runs and the three profiles are
retained as research evidence and close nothing; no further timing repetitions,
profiling or `ProtoShift` proof work happens until coverage is done.

**This is an incomplete, provisional record.** 60 cells: **3 YES, 2 BLOCKED,
55 PENDING.** The final checkpoint is gated on all 60 YES.

### 58.1 PicoRV32 whole core, the first row attempted

RTL `/HOMEROOT/projects/picorv32/picorv32.v`, 94,657 bytes, sha256
`0836050971b3c6cd…`, pin `ef203c2b0a3f…`, tracked tree clean. Top is
`picorv32`, the **whole core**. Shape: 1,302 sources, 2,279 nodes.

A **YES here means the artifact was generated AND its required generation check
ran and passed** -- not RTL equivalence, not full validation, not kernel
certification. Source emission alone is a phase, not a YES; the first version of
this section said otherwise and was premature.

| approach | status | emission | generation check | artifact |
|---|---|---|---|---|
| Verified compiler | **YES** | 4.01 s | **39.73 s** elaboration, olean 15,542,272 B; its **two** `native_decide` theorems ran and succeeded | `picorv32_Lgraph.lean` 279,483 B + `picorv32.dcert` 70,067 B + `_io.json` |
| Legacy | **YES** | 0.32 s | **87.87 s** elaboration, olean 70,343,304 B, 279 wf constructs evaluated | `picorv32_Lgraph.lean` 1,537,011 B + `_io.json`, emitted at `cert_wf=eval` |
| IR-as-semantics | **YES** | 0.06 s | Cert **9.13 s** olean 15,514,320 B; Main **0.84 s** olean + C 5,094 B | `Sims/picorv32_Cert.lean` 277,445 B + `_Main.lean` 396 B |
| Futamura projection | **BLOCKED** | -- | -- | -- |
| Translation validation | **BLOCKED** | -- | -- | -- |

Two corrections went into those rows. The legacy artifact was first emitted with
`formal.lean.cert_wf=skip`, which carries **no** well-formedness check at all;
it is re-emitted at `cert_wf=eval` (1,537,011 B, `LegacyCertWF` imported, 279
constructs) and that version is what elaborates. The `cert_wf=skip` artifact is
retained as phase evidence, not retracted. And the verified-compiler witnesses
are **trusted native execution, not kernel proofs**:

```
picorv32_compiles      [propext, picorv32_compiles._native.native_decide.ax_1_1]
picorv32_clock_bounds  [propext, Quot.sound, …native_decide.ax_1_1]
picorv32_step_correct  [propext, Classical.choice, Quot.sound,
                        picorv32_compiles._native.native_decide.ax_1_1]
```

No `sorryAx`. `native_decide` discharges through a per-theorem native axiom, so
these are checked by execution and trusted by the kernel, which is what the
generation check establishes and no more. The `lake exe Simpicorv32` link was
not performed; elaboration plus C emission is what is claimed for
IR-as-semantics.

Parameters are frozen: `picorv32.v` defaults as shipped at the pin, no `-D`/`-G`
overrides, whole core, **memory preserved** -- 1,302 sources, 2,279 nodes, 18
outputs, 174 flops, 1 memory.

The three successes are genuinely different artifacts from their own routes, not
one borrowed: legacy emits `picorv32_comb`/`_next`/`_step` plus
`_node`/`_source`/`_graph`; the verified compiler emits `picorv32_designCert`,
`_step`, `_residual`; IR-as-semantics emits a `DirectTrace` certificate and a
launcher. A generation YES is **not** validation, RTL equivalence or kernel
certification.

### 58.2 `single_edge` is required, not optional

A direct `compile verilog … --emit-dir lean:` fails:

```
[ERROR] pass.lean: unsupported certificate op(s): latch (first at n_1460).
68 node(s) in this design have no certificate encoding.
```

so the row's route is three steps -- `compile verilog → lg`, `pass single_edge
→ lg`, then the per-approach emit against the normalised graph. That is the
same requirement the pinned CORE-ET runner documents, now confirmed on
PicoRV32.

### 58.3 The generator binary cost a cell, silently

`bazel-bin` symlinks to **k8-fastbuild** (`lhd` sha256 `414d5dcbf99b37fe…`,
682 MB). Run from it, `pass.lean mode:verified_compiler` publishes the Lean
literal and the sidecar but **silently omits the `.dcert`** -- exit 0, zero
diagnostics -- although `pass_lean.cpp:88` publishes three items through
`publish_group`. The **k8-opt** binary (`9cdba78e6b236e8c…`, 203 MB), the one
that produced the existing dcert-bearing corpora, emits all three. Coverage runs
are now bound to the opt binary. The `_Lgraph.lean` is **byte-identical** between
the two builds; only the certificate differed. Recorded as an observed
behavioural difference, not diagnosed further -- that is optimization-adjacent
work and is queued.

### 58.4 The two blockers, precisely

**Futamura projection -- two independent prerequisites, not one.** The
certificate master emits is **DCERT2**
(`picorv32.dcert`, magic `DCERT2`, 1,302 sources, sha256 `f85a2a9db8ac89ba…`).
The FP driver refuses it: `bad magic: expected DCERT1`. The prerequisite is
fresh DCERT2 transport for FP -- the same gap section 46.4 recorded, now
confirmed on a requested target rather than inferred.

**Transport alone would not demonstrate support.** `CertLoad.supportReport`
requires `memFree` (`D.memories.size == 0`), and `picorv32` carries **one
memory** -- `cpuregs`, `aw 5`, `dw 32`, next-image slot 3225, clock 0 -- plus a
`memImg` source, which also fails the `sources` conjunct. So fixing the magic
would leave FP unsupported on this whole core. Memory support is a bounded
semantics and proof obligation, recorded as functional coverage work. The memory
is preserved, `memFree` is not removed, and no adequacy is asserted outside the
existing theorem.

**Translation validation.** The route is
`d3_sweep.py --manifest <tsv> --certs <dir> --out <dir> --jobs 1`, and it
consumes the **generated `_Lgraph.lean`** indexed from a directory, selected by
a manifest row -- not a `.dcert` (`--certs <file.dcert>` exits 2, "no targets
selected"). A one-row manifest (`picorv32`, `cert_available 1`, sha256
`8113993d2b3c670b…`, 2,279 nodes) resolves and selects the target, and then the
sweep **refuses**: *"REFUSING a manifest run from a dirty worktree: commit
first, or pass --allow-dirty for an exploratory run (its rows are not
reproducible)"*. The worktree carries authorized uncommitted supervisor files,
so a reproducible row needs those committed by their owner. An `--allow-dirty`
run would be exploratory and is not recorded as a YES.

### 58.5 Metrics recorded during coverage, and the guard scope

`pass/lean/scripts/fp_cov_run.sh` runs each cell under a delegated scope and
polls the cgroup every 0.5 s while the process lives, retaining the **last
sample taken before exit**.

> **CLAIM CORRECTION.** That is **bounded sampled evidence, not a guaranteed
> final peak or final OOM counter** -- the scope is torn down at exit, so
> anything between the last sample and teardown is unobserved. `memory.max` is
> a genuine enforcement read-back. It is still strictly better than the
> counterbalanced script's single startup sample, but it must not be called a
> whole-run final value. When a cell is too fast for a
sample to land it records
`observation_scope=NONE: … memory.max is REQUESTED only, not observed` rather
than promoting the requested limit -- which is what the 0.06 s IR-as-semantics
cell reports. Per cell: argv, start/end timestamps, load before and after, wall,
user and system CPU, max RSS, exit status, stdout and stderr, and the artifact
sizes and sha256s.

The `cb3` counterbalanced evidence keeps its corrected label: its
`memory.peak`/`memory.events` are **startup** observations, not whole-run
values.

### 58.6 Next

Bind and run the remaining eleven rows. The CORE-ET rows need the frozen
filelists already built in section 53; the CVA6 rows need their bender filelists
from section 54. Nothing in this section discharges the migration gates, the
fresh-16 or full validation, DCERT2 transport, the branch inventories, the
remaining D3/D4 work, the 447+6 jobs or the thirty raw-source comparisons.
Deferred D3 scoreboard timing and refactoring stay last.

## 59. The CVA6 route, resolved by running it wrong first (PROVISIONAL)

### 59.1 Corrections to section 58

**Two `native_decide` uses, not three.** The verified-compiler artifact uses
`native_decide` at lines 3815 and 3821; the third grep hit was prose in a
comment. Corrected above.

**`maxrss_kb` is KiB.** GNU `time`'s `%M` is KiB on this platform, so the
verified-compiler check's 6,929,248 is **6.61 GiB (7.10 GB)**, not 6.93 GB.
Raw KiB is what the records retain.

**IR-as-semantics readiness is elaboration plus C emission, not a link.** The
`lake exe Simpicorv32` link was not performed and is not claimed.

### 59.2 A per-top bender slice is the wrong CVA6 binding

Binding `CVA6 issue/scoreboard` and `CVA6 frontend` to `bender script
flist-plus --top <block>` produced complete-looking component lists --
`issue_stage` with `issue_read_operands`, `scoreboard`, `raw_checker`,
`ariane_regfile_ff/_fpga`; `frontend` with `btb`, `bht`, `bht2lvl`, `ras`,
`instr_scan`, `instr_queue`, `instr_realign` -- and then **failed to
elaborate**:

```
instr_queue.sv:392:23: error: invalid member access for type 'fetch_entry_t' (aka 'logic')
cvxif_issue_register_commit_if_driver.sv:44:20: error: invalid member access for type 'x_register_t' (aka 'logic')
```

21 errors on `frontend`, more on `issue_stage`.

**The root cause, precisely.** `issue_stage` and `frontend` declare a default
`CVA6Cfg = cva6_cfg_empty` and default their ten type parameters to `logic`.
Selecting the raw block as the top leaves those defaults in place, so the
member accesses are against `logic`. **Bender did not convert valid structs into
`logic`** -- the slice was fine; what was missing was concrete configuration and
types, which the AST-derived whole-unit wrappers supply. The whole-core 353-line
route is an established recipe that works, not evidence that every other
dependency slice is intrinsically incapable.

### 59.3 The real route, read off the existing corpus

`generated/master_port/cva6_foundation/.../provenance.txt` records what actually
produced the `<block>_gate` corpus, and it is three things I was not using:

* the **whole-core** filelist `cv64a6_imafdc_sv39_hpdcache_wb.flistplus.f`,
  353 lines, sha256 `9ae886ff867dcfd6…`, over **`cva6-blockfinding` @
  `6cb20010`** -- a different checkout from the `cva6-clean` @ `bc510437` I had
  been using;
* a per-block **gate wrapper** from `scripts/gen_cva6_wrappers.py`
  (sha256 `0dee9c9765b5a786…`), generated from slang's elaborated AST
  (278,287,750 B, sha256 `3699e6f57382…`); 28 generated, 0 not-in-config,
  2 failed; 29 wrappers on disk, including `frontend_gate.sv` and
  `issue_stage_gate.sv`;
* `formal.lean.max_width=1048576`, which the historical CVA6 runner sets and I
  had omitted.

With the wrapper and the whole-core filelist, elaboration and `single_edge`
pass. The emit then refused for a reason worth keeping:

```
[ERROR] pass.lean: node n_10692 (node) has width 2080 > max_width=1024
[ERROR] pass.lean: node n_684  (node) has width 3536 > max_width=1024
```

which is the documented `max_width` option, not a weakened predicate.

### 59.4 First non-Pico emissions

| row | approach | emission | artifact |
|---|---|---|---|
| B10 CVA6 frontend | verified compiler | 1.46 s | `frontend_gate_Lgraph.lean` **969,162 B** (`cb73b273…`) + `frontend_gate.dcert` 274,052 B |
| B10 CVA6 frontend | legacy (`cert_wf=eval`) | 1.64 s | `frontend_gate_Lgraph.lean` **5,422,167 B** (`cc5cb1b7…`) |
| B09 CVA6 issue/scoreboard | verified compiler | 0.77 s | `issue_stage_gate_Lgraph.lean` **1,252,028 B** (`4afcfb50…`) + `issue_stage_gate.dcert` 530,496 B |
| B09 CVA6 issue/scoreboard | legacy (`cert_wf=eval`) | 1.25 s | `issue_stage_gate_Lgraph.lean` **6,393,936 B** (`fc33a941…`) |

> **SIZE ATTRIBUTION CORRECTED.** The first version of this table swapped the
> verified-compiler and legacy sizes on both rows. The file headers settle it --
> the verified-compiler artifact opens
> ``-- Emitted by pass.lean in `formal.lean.mode=verified_compiler`.`` and the
> legacy one opens `/-` -- and the corrected direction matches PicoRV32, where
> the verified-compiler literal is also the smaller of the two. I read an `ls`
> ordering as an attribution and did not check the headers.

Upstream phases are separate measurements, not folded in: `frontend` elaborated
to an LGraph in 1.60 s and `single_edge` in 0.33 s; `issue_stage` in 9.64 s and
0.06 s.

**These are emissions, not YES.** Their generation checks -- elaboration of the
emitted Lean -- are **RUNNING**; the cells stay RUNNING until those pass. One
has landed: `B10` verified compiler elaborated in **80.46 s**, user 59.40 s,
peak **7,765,208 KiB = 7.40 GiB**, retained olean, exit 0. The other three are
queued or in flight and are **not credited**. A cap or a signal on any of them
is undecided/failed-check evidence, never a backend refusal.

### 59.5 Still open

The clean detached worktree at
`generated/master_port/cov12x5/sources/master_clean_3d8bb696c` (pin
`3d8bb696ce3cd712…`, status empty) is in place for translation-validation
admission; its private `.lake` is not yet prepared. FP keeps both prerequisites:
DCERT2 transport, and the memory gap -- `HardwareInterpreter.lean` is
byte-identical to the original `9bb41a09d`
(`58b8b9a4e5dc37ad3cb4ca6ed8eaff5b952ae7e54d4cd8b20b04d42bf584c572`), rejects
`memImg`/`memConst` and requires empty memories. That memory is preserved and
the restriction is not removed; the memory-interpreter adequacy obligation stays
open.

## 60. Approach-major sweep: LEGACY across all twelve rows (PROVISIONAL)

The queue is now **approach-major**: Legacy → verified compiler → IR-as-semantics
→ translation validation → Futamura, one approach across all twelve rows before
the next. Completed phases are credited, never rerun to repair a table label.

### 60.1 Legacy, 11 of 12 rows closed

| row | unit | legacy | reason |
|---|---|---|---|
| PicoRV32 whole core | `picorv32` | **YES** | emission 0.32 s, check 87.87 s, olean 70,343,304 B |
| CVA6 frontend | `frontend_gate` | **NO** | emission 1.64 s OK; **check exit 1**, 1711.92 s, 14,527,196 KiB (13.85 GiB): *frontend_gate_state.mk has too many fields (409)*, then `dependsOnNoncomputable` |
| CVA6 issue/scoreboard | `issue_stage_gate` | **NO** | emission 1.25 s OK; **check exit 1**, 1690.92 s, 19,488,268 KiB (18.59 GiB), same class |
| CORE-ET DCache | `minion_dcache_top` | **NO** | s1 exit 1, 6.91 s: *Concat lane 0 (of 2, MSB-first) driver `mux_94508` occupies 2 bits but its declared window is 1* |
| CORE-ET I-cache | `icache_top` | **NO** | s1 exit 1, 0.20 s: slang *multiple imports found for identifier `PaSize`* at `icache_pkg.sv:21:50` |
| CORE-ET TLB | `minion_tlb` | **NO** | s1 **passed**; `single_edge` exit 7: memory `memory_1660:u_tlb_cache_rf.rf_q` *would commit on every sub-step* |
| CORE-ET IntPipe | `intpipe_top` | **NO** | s1 exit 6, 4.19 s: *combinational loop*: sra → sext → get_mask → sra → … |
| CORE-ET VPU | `vpu_top` | **NO** | s1 exit 6, 50.74 s: *combinational loop*: mux → mux → sext → sext → sext → mux |
| CORE-ET ET-Link→AXI | -- | **BLOCKED** | `hw/ip/etlink/rtl` holds only `etlink_pkg.sv` and there is no `dv/rtlcosim` entry, so no whole unit is bindable in this checkout |
| CVA6 LSU | `load_store_unit` | **BLOCKED** | no gate wrapper; `load_unit`/`store_unit`/`store_buffer` exist separately and a per-component substitute is **not** accepted |
| CVA6 MMU/TLB/PTW | -- | **BLOCKED** | only `cva6_ptw` has a wrapper; the MMU and TLB components of the named group do not |
| CVA6 HPDcache | `cva6_hpdcache_subsystem_gate` | PENDING | wrapper exists, route bound, not yet attempted |

The two CVA6 **NO**s are the same capability blocker and deserve naming: the
emitted legacy state record has **409 fields**, which the current toolchain's
compiler IR check rejects, and `dependsOnNoncomputable` follows. That is a
**failed executable generation check, not a timeout and not an OOM** -- both ran
to completion in ~1700 s. Nothing was nested, no field was dropped, nothing was
marked `noncomputable`, no check was disabled, no backend was substituted and no
toolchain was upgraded to turn it into a YES. The successful emissions are
retained as phases.

> **CORRECTED in section 61.** Those five are **not** five distinct classes:
> two of them (`intpipe_top`, `vpu_top`) are the same combinational-loop
> refusal, and three more rows joined that class once their inputs were
> prepared. The classes are counted properly in 61.2.

### 60.2 Verified compiler already holds three, credited not rerun

`CVA6 frontend` check exit 0, 80.46 s, 7,765,208 KiB; `CVA6 issue/scoreboard`
exit 0, 91.75 s, 7,838,540 KiB; `PicoRV32` exit 0, 39.73 s. All three are
credited where they stand.

### 60.3 Queue defects fixed before relaunching

`fp_cov_queue.sh` is **retired** (it stubs out with exit 2) because it wrote
phases into fixed per-row directories and gated on `-d lg` after `|| true`. The
CORE-ET queue reproduced one more of my own bugs worth recording: it passed the
`.f` filelist as the positional source, so slang parsed the filelist as
SystemVerilog (*expected member*, *expected a declaration name*). The positional
is now an anchor `.sv` with the real sources arriving through
`yosys.filelist_file`, and the filelist's `+incdir+` is forwarded to slang.

### 60.4 Still open at their scheduled sweeps

IR-as-semantics executable readiness, translation-validation private-build
admission from the clean detached checkout, and Futamura's fresh DCERT2 loader
plus the inherited memory adequacy obligation all remain, each at its own sweep,
with no predicate weakened.

## 61. LEGACY sweep complete, with three of my claims corrected

### 61.1 The missing wrappers were an input gap, not a hardware limit

I recorded `CVA6 MMU/TLB/PTW` and `CVA6 LSU` as BLOCKED because no gate wrapper
existed. That was an **input-preparation gap**, and the supervisor closed it:
`scripts/gen_cva6_wrappers.py` and the already-frozen 278,287,750-byte AST
generate `cva6_mmu`, `cva6_ptw`, `cva6_shared_tlb`, `cva6_tlb` and
`load_store_unit` wrappers in 2.26 s. Those prepared files were **reused, not
regenerated**. `cva6_mmu` instantiates both `cva6_tlb` instances,
`cva6_shared_tlb` and `cva6_ptw`, so the **whole configured group** is bound,
and `load_store_unit` is the whole LSU -- no per-component substitute.

With the wrappers in place both units elaborate far enough to be **refused for a
real reason**, which is the point: a missing wrapper file was never a hardware
support limitation.

### 61.2 The refusal classes, counted properly

| class | instances | rows |
|---|---|---|
| **combinational loop** | **5** | CORE-ET IntPipe, CORE-ET VPU, CVA6 HPDcache, CVA6 MMU/TLB/PTW, CVA6 LSU |
| compiler IR, too many fields | 2 | CVA6 frontend (**409**), CVA6 issue/scoreboard (**334**) |
| concat window inconsistency | 1 | CORE-ET DCache |
| package imports | 1 | CORE-ET I-cache (`PaSize`) |
| declaration before use | 1 | CORE-ET ET-Link→AXI (`shirecache_pkg.sv`) |
| single-edge memory restriction | 1 | CORE-ET TLB (`rf_q` commits on every sub-step) |

Two corrections are embedded here. I called the first five CORE-ET refusals
"five separate findings"; they are **two** combinational loops plus three
others, and the loop class has since grown to five. And I reported
`issue_stage` as 409 fields; it is **334** -- only `frontend` is 409.

### 61.3 ET-Link→AXI: the unit exists

I wrote that no synthesizable ET-Link unit exists in the checkout. **Wrong.**
`shirecache_mesh_master_axi_port.sv` and `shirecache_mesh_slave_axi_port.sv`
import `etlink_pkg` and expose AXI channels, and `dv/rtlcosim` carries
`shirecache_mesh_master`, `shirecache_mesh_slave` and
`shirecache_l3_to_sys_bridge`. Running `shirecache_mesh_master` resolves a
17-file list and is refused by slang:

```
shirecache_pkg.sv:65:58  error: identifier 'TagDataSize' used before its declaration
shirecache_pkg.sv:105:64 error: identifier 'L3ShireIdSize' used before its declaration
```

a **declaration-before-use** defect -- the same class `coreet_patch_srcs.py`
already repairs for four other files with a pure decl reorder. That is the
precise bounded prerequisite; it is not an absent unit, and no unrelated AXI
block was substituted to close the cell. `shirecache_mesh_master_axi_port` has
no `rtlcosim` entry of its own, so no filelist resolves for it directly.

### 61.4 LEGACY is closed: 1 YES, 11 NO

Every one of the twelve rows now has a retained result from a real attempt.
PicoRV32 is the single YES. The other eleven are NO with an exact refusal
each -- no input-preparation gap remains in this sweep, and no refusal was
turned into a YES by weakening anything. The two field-limit failures are left
**unchanged and unretried**.

### 61.5 Dispatcher fixes made before this sweep ran

`fp_cov_queue_cva6.sh` always emitted verified-compiler first and legacy second,
which contradicts the user's approach order and would have rerun completed work.
It now takes the approach as an argument and runs **only** that one; `WRAP=`
overrides the wrapper path so prepared inputs are reused in place; an explicit
`ATTEMPT` collision is **refused** rather than reusing the directory; and the
phase gate parses the **top-level** `status` field with a JSON parser instead of
grepping for a `"pass"` substring that a phase name or diagnostic could carry.

Next sweep: verified compiler across all twelve rows, crediting the three
already-passing checks (PicoRV32 39.73 s, frontend 80.46 s, issue/scoreboard
91.75 s) without rerunning them.

## 62. Verified-compiler and IR-as-semantics sweeps (PROVISIONAL)

60 cells: **5 YES, 11 NO, 20 BLOCKED, 2 RUNNING, 22 PENDING.**

### 62.1 Verified compiler: 3 credited, 9 blocked on an already-failed prerequisite

`PicoRV32` (39.73 s), `CVA6 frontend` (80.46 s) and `CVA6 issue/scoreboard`
(91.75 s) are **credited where they stand** -- not rerun.

For the other nine, the verified-compiler route shares the **identical** upstream
prerequisite with legacy: the same RTL→LGraph compile and `single_edge`, the
same `lhd` binary, the same options, the same filelist or wrapper. That
prerequisite already failed for each of those rows, so each is **BLOCKED**,
linked to the exact original failure and its metrics. **The verified-compiler
emitter was never reached and is not claimed tested**, and the identical failed
frontend was not rerun to decorate a second approach.

### 62.2 IR-as-semantics: PicoRV32's readiness phase actually executed

The missing phase is done, once, with real work:

```
lean --run Sims/picorv32_Main.lean picorv32 --cycles 8 --show-state
exit 0, wall 1.30 s, user 0.78 s, sys 0.33 s, 477,764 KiB
# lgraph-sim picorv32 sources=1302 nodes=2279 outputs=18 flops=174 mems=1 inputs=9 clocks=1
```

18 lines retained, and a no-argument probe first returned the launcher's usage
with exit 2, confirming the entry point loads.

> **TWO OVERSTATEMENTS RETRACTED.** I wrote that the state advances across
> cycles and that the run exercises the memory. Recounted from the retained log:
> the 8 flop lines are **all identical** -- one unique vector, so the flop state
> does **not** advance -- and the 8 output lines hold **two** unique vectors,
> an all-zero cycle 0 followed by seven identical non-zero cycles. `mems=1` is a
> **header field**; it establishes no memory read or write transition. My
> earlier count of "3 unique" came from a loose grep that also matched the
> header line. The run is still a genuine execution of the generated simulator
> -- which is what the readiness phase claims -- and no memory-validation
> experiment is required at this checkpoint.

**Honestly scoped: this is the INTERPRETED route** (`lean --run`), not a native
link. `lake exe Simpicorv32` has still not been performed, and no emission or
elaboration was repeated to obtain the label.

`CVA6 frontend` and `issue/scoreboard` IR artifacts were generated from their
**already-produced** verified-compiler modules -- `frontend_gate_Cert.lean`
966,984 B + `_Main.lean` 416 B, `issue_stage_gate_Cert.lean` 1,249,766 B +
`_Main.lean` 428 B -- with no upstream rerun. Their checks and readiness are
**RUNNING**, not credited.

The nine remaining rows are **BLOCKED**: `make_sim_launcher.py` consumes a
generated `<top>_Lgraph.lean` and nothing else, and that module exists only if
the verified-compiler emitter ran. It did not, so this route has no independent
usable input for them.

### 62.3 Records and runner hardening

The nine failed legacy phase records were taken from the supervisor's extraction
of the retained GNU `time` logs into `phase_times_ms`, `peak_rss_kb` and
`attempt_record`, with their scope preserved: failed upstream phases, source
emission **NOT_REACHED** rather than zero, RSS in raw KiB, and no fabricated
launch-time environment binding. **No row says `input_mapping=PENDING` any
more**: all nine carry the real whole-unit, component, source-pin, route and
option binding.

Both dispatchers were fixed **before** this sweep launched: attempt directories
are created with a plain `mkdir` so a collision is refused atomically rather
than reused by `mkdir -p`; the provenance `BINDING.txt` is written **before** any
phase executes; the approach argument is validated up front; and the CORE-ET
runner now parses the **top-level** JSON `status` and confirms the **intended
top** is in the emitted library instead of matching a substring.

`B05b` (the direct `shirecache_mesh_master_axi_port` attempt) is retained as a
**failed preparation/characterisation attempt**: it stopped before any
compilation because `coreet_filelist.sh` found no filelist for that module, and
it is not evidence of a successfully configured whole group.

The HTML matrix was refreshed from the JSON with the supervisor's
`refresh_coverage_matrix.py`, so JSON, TSV, HTML and ledger agree.

## 63. IR-as-semantics closed; the private TV root validated and the TV sweep launched

### 63.1 The two CVA6 IR checks reached a terminal, successful result

`fp_cov_ir_check.sh` ran the two retained rows serially and both finished with
every phase exiting 0.  Read back from the retained cells rather than from the
worker's own summary line:

| row | Cert | Main | 8-cycle interpreted run |
|---|---|---|---|
| B10 `frontend_gate` | exit 0, wall 53.50 s, user 52.58, sys 1.68, RSS 1,796,308 KiB | exit 0, wall 0.88 s | exit 0, wall 49.55 s, user 48.97, sys 0.33, RSS 492,372 KiB |
| B09 `issue_stage_gate` | exit 0, wall 56.29 s, user 55.94, sys 1.23, RSS 1,734,812 KiB | exit 0, wall 0.68 s | exit 0, wall 269.56 s, user 268.91, sys 0.36, RSS 492,452 KiB |

What that is and is not: **usable INTERPRETED generation** — `lean --run` on the
generated launcher, not a native `lake exe` link, not comprehensive validation
and not kernel certification.  The 49.55 s and 269.56 s are **eight-cycle
execution time, not source-generation latency**; they belong to no generation
metric.  The phases and their artifact identities are preserved and were not
re-run to produce this record.

All twelve IR dispositions are now present: **3 YES, 9 BLOCKED** on the
previously verified missing input — the nine rows have no `<top>_Lgraph.lean`
because the verified-compiler emitter was never reached, which §62.1 already
established from the shared upstream stop.

### 63.2 The worker's exit propagation was wrong, and is now fixed and self-tested

`fp_cov_ir_check.sh` ended in `echo "$ROW IR readiness exit=$e ..."`, so **the
wrapper returned 0 even when the run exited nonzero** — a cap or a refusal would
have been handed to its launcher as success.  The retained procedure now reads
each phase's true producer exit from both the cell's `exit` file and GNU `time`'s
`exit %x`, requires the two to agree, and exits with the real code.  Outcomes are
classified rather than collapsed:

| label | meaning | propagated exit |
|---|---|---|
| `OK` | producer exited 0 | 0 |
| `CAP` | `timeout` fired — partial, neither pass nor refusal | 124 |
| `REFUSED` | the producer itself failed | its own code |
| `MISSING` | no exit record was retained for that phase | 97 |
| `DISAGREE` | wrapper rc and measured producer rc differ | 96 |

Self-tested against synthetic cells for all four non-OK classes; the `DISAGREE`
case (wrapper 0, producer 1) is the one that matters, and it is now reported as a
failure instead of a pass.  A read-only `--classify <row>` mode re-derives a
retained tree's disposition **without running anything**, and it reproduces
`exit=0` for B09 and B10 from their retained artifacts — so the already-passed
jobs were not re-run to validate the fix.

### 63.3 The private TV root: what was actually checked

The supervisor's independent `cp -a --reflink=auto` (pid 2730281) finished:
`wall 713.95 s, user 0.85, sys 32.31, exit 0`.  I started no duplicate copy and
removed nothing.  Validation of the destination, all of it measured:

* **14/14 critical artifacts** under
  `.lake/build/lib/lean/LeanSemanticPrimitives/Compiler/` are **hash-equal to
  the canonical audited values**, on **distinct inodes**, each with
  **`nlink=1`** — no hard-link alias to the live compiler.
* `formal/lean/.lake` is a **real directory, not a symlink**; `build_root()`
  therefore resolves inside the private `ROOT` and `build_root_is_external()`
  returns false.
* **134,380 files / 13 GiB in both trees** (`du -h`, 1024-based); **3 symlinks**, all relative and
  internal to `packages/` (batteries' `docs/README.md`, two mathlib `scripts/`
  entries).  None escapes the private root.
* `lake env` resolves **every** `LEAN_PATH` entry under the private checkout,
  plus the shared read-only elan toolchain lib.  No live-tree path leaks in.
* The private checkout is at `3d8bb696ce3cd712240368c9a82c434f8ba31e18` with an
  **empty `git status --porcelain`**, before and after the build.

**Copied traces are not valid traces in a new root.**  Lake records the
**absolute source path** as a trace input, so in the private root every local
module keys differently and reads out-of-date.

**CORRECTION — the freshness claim first entered here was wrong.**  It said a
private `lake build --no-cache` (exit 0, wall 18.20 s, user 14.72, sys 4.50,
maxrss 800,444 KiB = 0.76 GiB) had re-validated all fourteen critical compiler
traces.  **It had not.**  That invocation ran the DEFAULT target, which is the
library root's import closure — **7 jobs** — and the fourteen critical
`Compiler/*` modules are not in it.  Afterwards `lake build --no-build` with no
target list reports `All targets up-to-date` about those same 7 jobs, and I read
that as covering the closure the probes load.  It does not.

The explicit check is `lake --no-build build` naming all fourteen targets, and
the supervisor ran it:

* **before** the Projection build: **exit 3**, `[3/50] Building
  LeanSemanticPrimitives.Compiler.DesignCert — target is out-of-date and needs
  to be rebuilt`;
* **after** the 34-job Projection build: still **exit 3**, now at
  `[5/76] ... Compiler.ResidualIR`.

Raw evidence:
`automate_workspace/master_rebase/audits/coverage_snapshot_212454_checked_2126/private_no_build_status.json`
and `private_no_build_after_projection.json`.

So for this closure: the fourteen artifacts are **hash-equal to the canonical
audit on distinct inodes with `nlink=1`**, and that is **preparation**;
**default-target success is not validation of it**, and their traces are still
unvalidated in the private root.  Building the fourteen explicitly is a separate,
still-owed step, and it must wait — see §63.7.

What the TV run does bind, independently of traces, is the bytes it loaded: the
sidecar's `artifact_digest = faa1ca2c0818ac6c8d4d549534469d27ac4e61551dea9e4c1b7314235961472e`
recomputes exactly over those fourteen private `.olean`s, which are byte-identical
to the audited ones.  That is an identity of loaded artifacts, not a fresh build.

No shared mutable build-root alias, no hard link to the live compiler, no
`--allow-dirty`, no `D3_ARTIFACT_DIR` test seam.

### 63.4 The TV manifest carries all twelve rows, including the nine with nothing to run

`generated/master_port/cov12x5/tv12x5/manifest.tsv` has **one row per required
benchmark**.  Three carry `cert_available=1` with the exact certificate hash and
node count, independently cross-checked:

| module | bytes | sources | nodes | outputs | flops | mems | cross-check |
|---|---|---|---|---|---|---|---|
| `picorv32` | 279,483 | 1,302 | 2,279 | 18 | 174 | 1 | sources match the `DCERT2` header; nodes match the pre-existing manifest row |
| `frontend_gate` | 969,162 | 6,585 | 6,503 | 3 | 409 | 0 | every field matches the IR run's own header line |
| `issue_stage_gate` | 1,252,028 | 6,896 | 6,947 | 40 | 334 | 0 | flops match the §62 count correction |

The other nine carry `cert_available=0` and a **linked** `blocked_cause` naming
the exact upstream stop already on record — eight `s1_compile` refusals (one
concat-lane width, two slang declaration errors, five combinational loops) and
CORE-ET TLB's `pass.single_edge` exit 7.  **No identical failed RTL compilation
was replayed**, no tiny stand-in was substituted and no component was erased.

### 63.5 The TV sweep itself

`cgroup v2 delegation is unavailable from this session's shell` — the session
sits in `session-886.scope`, whose `cgroup.subtree_control` is empty, and
migrating into the `user@.service` subtree crosses a root-owned common ancestor.
`--enforce cgroup` correctly refused rather than silently downgrading to the
sampled advisory limit.  The fix is to launch the sweep **inside** a
`systemd-run --user --scope`, where the delegated preflight passes; the memory
guard is therefore real and kernel-backed, not sampled.

Launched from the private checkout, with `ROOT` that clean source and
`D3_LEAN_ROOT` its private `formal/lean`: driver
`d3_sweep.py sha256=5e3f7cdc…`, manifest digest `70395ea8138ef29a`, selection
`faeaa352b8af0b25`, reifier `legacy` (the default), `--jobs 1 --timeout 1800
--enforce cgroup --kill-over-rss-kb 16000000`.  `--prove` was **not** requested:
this is the generation/admission sweep, and the proof gate is a separate
obligation.  The dry run resolved 3 targets to run and 9 manifest rows without a
certificate, which the sweep deliberately does not write — the join owns them.

### 63.6 Futamura: the DCERT2 loader prerequisite, established as an observation

The FP generation driver `gen_probe.lean` loads through `CertIO.V1.loadCert` —
FP's **DCERT1** codec.  Every certificate the current C++ pass emits is
**DCERT2**, so that driver cannot read any of them.  That, and not a projection
limitation, is the FP column's missing functional prerequisite.

`formal/lean/scripts/gen_probe_v2.lean` is the DCERT2-capable driver.  It is
`gen_probe.lean` with one substitution — `Compiler.CertIO.admitAny` in place of
`CertIO.V1.loadCert` — and `gen_probe.lean`'s CODE is unchanged, so the existing native
generation-latency procedure and its paired numbers stay comparable.  Its header
did change, once: the claim that the excluded `szOf` traversal was "the single
most important difference" between its numbers and `total_probe`'s is **refuted
by measurement** — szOf reports **0 ms** on every run that reaches it, including
2,213,885 terms of CVA6 frontend residual — so the comment now says the exclusion
is measurement hygiene rather than a cost.  Both source identities are retained:
`b6dff38b…` is what the measured native binary was built from, `36608266…` is the
corrected file.  `admitAny` routes on the magic and then applies **each format's own
admission**: DCERT2 through `readCertChecked` (parse, then `certWfError`), DCERT1
through FP's parse-then-`asyncOK`.  No policy is widened, narrowed or replaced
with a third, and `magics_are_exclusive` proves the routing cannot mis-route.

`formal/lean/scripts/dcert2_loadcheck.lean` answers the narrower question —
*can the DCERT2 bytes be admitted at all* — by importing only `CertIOFacade`.
Run against the three real certificates (`lean --run`, wall 89.06 s, user 5.01,
sys 7.81, maxrss 6,288,540 KiB ≈ 6.0 GiB, exit 0):

| certificate | bytes | detect | admitted | sources/nodes/outputs/flops/mems/clocks | `asyncReset` flops |
|---|---|---|---|---|---|
| `picorv32` | 70,067 | `v2` | under v2's own policy | 1302 / 2279 / 18 / 174 / 1 / 1 | 0 |
| `frontend_gate` | 274,052 | `v2` | under v2's own policy | 6585 / 6503 / 3 / 409 / 0 / 1 | **409** |
| `issue_stage_gate` | 530,496 | `v2` | under v2's own policy | 6896 / 6947 / 40 / 334 / 0 / 1 | **333** |

Two things this establishes rather than asserts.  First, every parsed shape
**matches the field counts computed independently from the emitted
`<top>_Lgraph.lean`** — two unrelated artifact formats agreeing field by field.
Second, the metadata DCERT1 **structurally cannot carry** is present and
non-trivial: 409 of 409 frontend flops and 333 of 334 issue-stage flops carry
`FlopDesc.asyncReset`, and PicoRV32's single memory carries a
`MemoryDesc.clock`.

**Scope, corrected.**  `dcert2_loadcheck.lean` decodes and admits and does
**nothing else** — it evaluates no `supportReport`, no conjunct and no
projection, so none of its output is evidence about supportedness.  The six-field
reports come from `dcert2_support.lean` and from the `gen_probe_v2` runs, and
those are what §63.8 records.  What the decode result does establish is that the
metadata the support predicate later reads is really present in the bytes.

**What is still missing, stated precisely.**  `scripts/build-opt.sh` — the
standalone native route the FP probes are built by — deliberately builds
`Compiler/CertIOV1` and **not** `Compiler/CertIO`, because the DCERT2 codec
reaches `Compiler/CompileDesign`, which does `import Mathlib`.  So
`gen_probe_v2.lean` cannot be built by that route as it stands; it needs the
Projection chain built where Mathlib is available, and the private lake root has
Mathlib but has **no** `Projection/*` oleans (the lake default target closes over
the library root only — 7 jobs).  Building that chain under lake is the next
prerequisite, and it is a real build cost, not a generation measurement.

**And the memory gap is unchanged.**  `SupportedByProjection` still has six
conjuncts including `memFree`, so PicoRV32 — the one whole-core row, with
`memories = 1` — remains refused as unsupported.  That is the inherited
projection semantics / runtime-relation / adequacy gap.  `memFree` was not
removed, no memory was erased from any design, and no design is claimed
supported that is not.

### 63.7 A build overlapped a live importer, and the explicit 14-target build is still owed

**The overlap, recorded rather than excused.**  While the TV sweep was running
and its probes were importing `.olean`s out of the private `.lake`, I started a
34-job `lake build --no-cache` of the Projection chain **into that same
`.lake`** (wall 97.21 s, user 231.48, sys 21.12, maxrss 1,868,360 KiB = 1.78 GiB,
exit 0).  It happened to rebuild `Compiler.Runtime` and `Compiler.DesignCertWF`,
and `Runtime` is one of the fourteen artifacts the sweep's `artifact_digest`
covers.  I checked afterwards and all fourteen still hash equal to the canonical
audit, so no drift was detected and the sweep was not aborted — **but equal
hashes afterwards do not prove isolation.**  The window existed; a differing byte
would have been detected only as drift, and a replaced file mid-import could have
failed in ways a digest comparison taken later cannot see.  It was an
unnecessary risk to take against a canonical run and it should not have been
taken.

**Rule adopted:** no further writes into a build context that has live
importers.  Either wait for the consumers to reach a terminal result, or use an
independently owned output/dependency root.  Read-only use (`lean --run` against
a `LEAN_PATH`, which writes nothing into `.lake`) is not a write and is not
covered by this.

**Still owed, and deliberately deferred until the consumers are terminal:** a
private, explicit build of the fourteen canonical `Compiler` targets —
`CompileDesignDefs`, `CompileGraphDefs`, `CompileOpDefs`, `CompileDesign`,
`CompileGraph`, `CompileOp`, `D3Harness`, `ReifyGen`, `Reify`, `ReifyProof`,
`Runtime`, `DesignCert`, `ResidualIR`, `ResidualSemantics` — capturing full
argv/cwd/source/dependency identities, true exits and artifact hashes, and
required to be followed by an explicit 14-target `lake --no-build build` that
exits **0**.  Dependency-preparation cost stays separate from any generation
number.  If any artifact changes, both identities are kept and only the affected
evidence is re-obtained; nothing that already passed is blindly re-run.

### 63.8 Futamura on DCERT2: what ran, and what each phase does and does not claim

**Four phases, four different claims**, and the runnable procedure is
`pass/lean/scripts/fp_cov_dcert2.sh` — committed, not described:

1. **decode** (`dcert2_loadcheck.lean`) — the bytes are admitted under the
   format's own policy.  **Evaluates no support predicate.**
2. **support** (`dcert2_support.lean`) — the six conjuncts, and the concrete
   sources/operators/memories responsible when one is false.
3. **generate** (`gen_probe_v2.lean`) — specialize + `Hw.checkResidual`.
   Produces an **in-memory** `Program` and **executes no residual step**.
4. **readiness** (`gen_readiness_v2.lean`) — `Hw.stepOf` at the checked bound.

A `GENERATION OK` at phase 3 is therefore **not** a usable-simulator result; the
matrix's YES needs phase 4.  Phase 4 **re-specializes**, because phase 3
serializes nothing and there is no retained `Program` to load: that makes it a
**new attempt with its own cost**, never a second measurement of the generation
metric.

**PicoRV32's refusal, named concretely.**  `supportReport` gives
`wf true, memFree false, sources false, ops false, arities true, flopClocks true`
— and the three falses are three different gaps, not one restated:

* `memFree` — 1 memory: `aw 5, dw 32, nextImg 3225, clock 0` (the register file);
* `sources` — 1 unsupported source: `memImg(idx 0, aw 5, dw 32)`;
* `ops` — 3 nodes on operators outside the seventeen `OpSupported` admits:
  `Op_MemRead ×2` and `Op_MemWriteBE 1 ×1`.

So whole-Pico is blocked by memory **semantics** (`memFree`), memory **sources**
and memory **operators** together.  Nothing was removed, erased or weakened to
say so, and the inherited runtime-relation/adequacy gap stands.

**Both CVA6 units are `SupportedByProjection` on all six conjuncts**, and both
generate.  The route needed a configuration the default does not provide: at the
default `lean --run` stack both abort with *deep recursion … at 'interpreter'*
inside `Projection.mixTerms`; `--tstack=262144` **alone did not help**;
`ulimit -s unlimited` together with `--tstack=1048576` gets past it.  That is a
route/configuration requirement and is recorded as one — **not** an optimization
result and **not** a regression — and no further stack experiments were run.

| row | fuel | result | measured total | proc wall | maxrss | checker bound | szOf |
|---|---|---|---|---|---|---|---|
| `frontend_gate` | 20000/200 | **GENERATION OK** | 341,773 ms | 385.39 s | 6,707,152 KiB = 6.40 GiB | 31,753 | 2,213,885 terms in **0 ms** |
| `issue_stage_gate` | 20000/200 | OUT OF FUEL at this budget | 859 ms | 60.65 s | 6,592,540 KiB = 6.29 GiB | — | — |
| `issue_stage_gate` | 200000/5000 | **GENERATION OK** | 384,065 ms | 429.62 s | 6,809,596 KiB = 6.50 GiB | 32,369 | 2,926,014 terms in **0 ms** |

Every failed configuration is retained alongside the successful one; the
out-of-fuel row is a failure **at that budget**, not a capability rejection.

These are **interpreted** numbers and are not comparable to
`fp_generation_latency.tsv`, which is the native −O2 binary on DCERT1
certificates at a different fuel.  **Native linking is a separate readiness mode
and is not implied by any interpreted run here.**  The native route
(`scripts/build-opt.sh`) builds `Compiler/CertIOV1` and **not** `Compiler/CertIO`,
because the DCERT2 codec reaches `Compiler/CompileDesign`, which does
`import Mathlib`, and this workspace's mathlib package currently ships oleans
with **no** native objects (`find .lake/packages/mathlib/.lake/build -name '*.o'`
returns 0, and there is no `.a` or `.so`).

**Scope of that observation, corrected.**  It establishes a MISSING PREREQUISITE
in this route — the native artifacts are not present and would have to be
produced — and nothing more.  It is **not** a finding that such a build is
impossible, and an earlier phrasing of mine ("this workspace cannot build
native") overstated it.  What the cost of producing them would be was not
measured, and that build is not started here: it is optional work that sits
behind coverage.

The DCERT1 blocker is confirmed by execution rather than inference: the retained
native driver (`sha256 b7abe8e0…`) refuses both DCERT2 certificates with
`bad magic: expected DCERT1`.

### 63.9 Readiness passed on CVA6 frontend; the procedures were hardened and the commit claim corrected

**CORRECTION — "committed as scripts" was false when I said it.**  At the
reviewed checkpoint, `HEAD` was `1d3407f8d` and
`pass/lean/scripts/{fp_cov_dcert2.sh,tv_cov_sweep.sh}`,
`formal/lean/scripts/{gen_readiness_v2.lean,dcert2_support.lean}` and the new
`tests/fp/*` tables were all **untracked**.  They existed on disk; existence is
not a commit.  They are committed with this checkpoint, and no claim of the form
"already committed" should have been made before that.

**`tv_cov_sweep.sh` is a RETAINED RECONSTRUCTION, not the thing that ran.**  It
was written after the live TV sweep had already been launched by hand, and it has
not itself been invoked.  Its argv, environment and gates are that run's — that
is what makes it a reconstruction rather than a sketch — but no retained evidence
was produced by it, and its header now says so.

**Frontend readiness actually passed**, and it is genuine interpreted readiness:

```
8 cycles of Hw.stepOf at the checked bound: ok,ok,ok,ok,ok,ok,ok,ok
distinct flop-state vectors 5 of 9    distinct output vectors 8 of 8
ms: load 378  support 20  specialize 346966  check 1281  execute 70500
process: wall 460.89 s, user 424.18, sys 11.58, maxrss 6,707,540 KiB = 6.40 GiB, exit 0
```

The state really does advance — five distinct flop vectors across nine recorded
states, eight distinct output vectors — which is why the driver counts distinct
vectors rather than asserting motion.  This is **not** equivalence, **not**
acceptance, **not** a reference comparison and **not** kernel certification, and
it is interpreted, so it implies nothing about native linking.  It was not
repeated.

**Boundary defects found by static review and fixed.**  Each was a way to earn a
pass by doing nothing:

| defect | behaviour | now |
|---|---|---|
| `gen_readiness_v2 cycles=0` | `List.range 0` ran no step, `bad` stayed `false`, exit 0 `READINESS OK` | refused **before** load or generation |
| malformed fuel/cycles | `getD` silently substituted a different budget than the command line asked for | refused, with the offending token quoted |
| `fp_cov_dcert2.sh` with no certificate | swept nothing, exited 0 | refused |
| `tv_cov_sweep.sh` re-using an `OUT` root | `mkdir -p` overwrote a retained attempt's binding, logs and results | atomic `mkdir`, refuses an existing root |
| wrapper `rc=0` with no GNU-time record | credited as a pass although the measurement was lost | **MISSING → 97**, never a pass |
| `bash -c` with unquoted `$*` | a path containing a space was split into several argv entries | argv passed as an array through positional parameters |
| LEAN_PATH check | accepted any string merely *containing* the private root | **every** entry must be under the private closure or the one allowed immutable toolchain |

Verified without re-running any heavy generation: the four Lean guards refuse in
13–20 s each (Mathlib load only — no certificate is read); the six shell guards
refuse with the expected codes; the five outcome classes `OK / CAP(124) /
REFUSED / MISSING(97) / DISAGREE(96)` are distinct on synthetic cells; the array
form preserves five argv entries including two paths with spaces where the `$*`
form splits them into seven fragments; and the path check **rejects** a LEAN_PATH
with the live shared build root spliced in while accepting the real private one.

**Reuse instead of replay.**  `fp_cov_dcert2.sh` now reports a retained
`exit 0` phase as `REUSED` and does not re-run it, and `--only readiness` goes
straight to readiness for certificates whose earlier phases already passed.
Measured: a reused decode phase returns in **1.75 s** and launches no `lean`.
Redoing a phase deliberately needs `--force`.  A passed hour-long generation is
never re-run to reach the phase after it.

Both readiness drivers' source identities are kept: the frontend result was
produced by `dc7ec7d5…` (pre-guard), the issue-stage attempt runs
`93761c10…` (post-guard).  The guards reject `cycles=0` and malformed
configuration only; the 8-cycle path is unchanged between them.

### 63.10 All twelve TV and all twelve FP dispositions are now recorded

The TV sweep reached a terminal result: **exit 0, wall 3130.87 s, user 1339.49,
sys 36.77, maxrss 8,171,064 KiB = 7.79 GiB**, aggregate RSS peak 8,227,316 KiB =
7.85 GiB against the 20,000,000 KiB cap.

| target | verdict | wall | maxrss |
|---|---|---|---|
| `picorv32` | **agree**, all seven gates | 62.77 s | 2,389,304 KiB = 2.28 GiB |
| `frontend_gate` | **agree**, all seven gates | 1260.38 s | 8,171,064 KiB = 7.79 GiB |
| `issue_stage_gate` | **timeout** at the 1800 s cap (ran 1803.11 s) | — | — |

`proof` reads `0/3 (3 na, 0 failed)` — `--prove` was not requested, so the gate
is *not applicable*, not failed.  PicoRV32's controls are worth stating
precisely: `selftest_base=1`, `mut_out` rejected, `mut_flop` rejected and
**`mut_mem` rejected** with `mutable_mem=1` — its memory was **validated, not
bypassed**, which is the exact opposite of the FP column's position on the same
design.  32 samples, 32 distinct observables.  This is admission of the
generated certificate: **not standalone emission latency and not a full kernel
proof.**

**A cap is UNDECIDED.**  `d3_sweep` classifies a timeout as NONTERMINAL and says
so in its own summary — *"resumable, and NOT a gate failure"*.  So
`issue_stage_gate` is neither a pass nor a capability `NO`.  `--timeout` is part
of the sidecar config and `--resume` refuses when it changes — deliberately, a
different budget being a different experiment — so the re-attempt runs in a
**new attempt root** (`tv12x5/run_issue_7200`, `--only issue_stage_gate
--timeout 7200`), and the capped attempt is retained untouched.  That re-attempt
was launched **by `pass/lean/scripts/tv_cov_sweep.sh` itself**, which therefore
is no longer only a reconstruction for this attempt: its `argv.txt` and
`BINDING.txt` were written by the run.

**The nine unavailable rows carry the same inherited common-input blocker in
both columns** — no `<top>_Lgraph.lean`, because the verified-compiler emitter
was never reached when the shared RTL→LGraph / `single_edge` prerequisite failed
— each linked to its exact upstream stop, and **no identical failed RTL
elaboration was replayed** to establish any of them.

Matrix state across JSON, TSV, HTML and this ledger: **10 YES, 11 NO, 37
BLOCKED, 2 RUNNING — 60 cells, none PENDING.**  The two RUNNING cells are
`B09`'s TV re-attempt and `B09`'s FP readiness, both with live PIDs recorded.
The all-60-YES gate is **not** met and nothing is pushed.

### 63.11 The Futamura column is complete: both CVA6 units reach executed readiness

`issue_stage_gate` readiness is terminal and passed, at the budget at which its
generation succeeded:

```
8 cycles of Hw.stepOf at the checked bound: ok,ok,ok,ok,ok,ok,ok,ok
distinct flop-state vectors 5 of 9    distinct output vectors 8 of 8
ms: load 614  support 20  specialize 381876  check 1685  execute 298048
process: wall 703.49 s, user 685.74, sys 11.23, maxrss 6,830,964 KiB = 6.51 GiB, exit 0
residual 2,926,014 terms, checker bound 32,369
```

Both CVA6 units therefore pass the same four phases, and the two readiness runs
agree on the shape of the result — **5 distinct flop-state vectors across 9
recorded states and 8 distinct output vectors** on each — so the state really
advances under the residual.  That is counted, not asserted; the driver prints
the distinct counts precisely so a never-moving vector could not be narrated as
motion.

**All twelve FP dispositions:** 2 YES (`frontend_gate`, `issue_stage_gate`),
1 BLOCKED on three concretely named inherited gaps (`picorv32`: `memFree`,
`memImg` source, `Op_MemRead`/`Op_MemWriteBE` operators), and 9 BLOCKED on the
inherited common-input blocker shared with the TV column.

What this is not, restated because the phase names invite the wrong reading: the
readiness runs are **INTERPRETED** executions of the residual through the
projection's own checked simulator.  They are not equivalence, not acceptance,
not a reference comparison against `interpretDesign`, not kernel certification,
and they imply nothing about native linking, which remains a separate readiness
mode whose prerequisites this route does not currently have in place — see §63.8
for what that observation does and does not establish.  The specialization
each readiness run performs is a **new attempt** forced by the ephemerality of
the generated `Program`, and its cost is not a second generation measurement.

Matrix: **11 YES, 11 NO, 37 BLOCKED, 1 RUNNING** over 60 cells.  The single
RUNNING cell is `B09`'s TV re-attempt at the 7200 s budget.

### 63.12 A real false pass in the reuse path, and the guards that now stop it

**The counterexample.**  A retained attempt with `exit` = 0 but GNU time
reporting `exit 1` was **reused as a pass**.  The reuse branch read only the
`exit` file and skipped the producer-status validation that a fresh phase
performs — so a cell whose two records contradict each other was credited — and
it wrote `REUSED exit=0` **over the retained `DISPOSITION.txt`**, destroying the
record it claimed to be honouring.  It checked no certificate, source, fuel,
cycle, tool or dependency identity, and `main` overwrote the module binding
*before* deciding reuse, so the binding of the run that produced the retained
attempt was replaced by the request that reused it.  `--force` and failed
retries wrote into the same phase directory despite the comment promising a new
attempt root.

**Attempts are now immutable.**  Every run writes a fresh
`<phase>/attempt-NNNN-<ts>/` and nothing ever writes into an existing one — not
a retry, not `--force`, not a reuse decision.  Reuse verdicts live in
`<phase>/decisions/`, per-request bindings in `<module>/requests/`, and an
append-only `<phase>/index.tsv` lists the attempts.  A retained attempt's
binding, logs, exit, timing and disposition are never touched.

**Reuse is earned.**  All of these must hold, or the request gets a new attempt:
`exit` present, parseable and 0; a parseable GNU-time producer status that is
also 0 and agrees with it; a `DISPOSITION.txt` beginning `OK`; and a
byte-identical `IDENTITY.txt` — same certificate **content**, same driver
**content**, same lean binary and version, same resolved `LEAN_PATH`, same argv
(which carries fuel and cycles) and the same timeout.  So reuse can never
relabel an old result as a fresh source run, and a driver edited in place cannot
be mistaken for the one that ran.

Verified by `pass/lean/scripts/fp_cov_dcert2_reuse_test.sh`, which extracts the
**live** functions from the dispatcher and drives them against synthetic
attempts with a stub `lean` — no Lean, no certificate, no generation, under a
second.  All eight cases pass, and `retained_unchanged` is **YES in every one**,
including the reuse case.  Case 1 is the audited counterexample, now refused.

**The LEAN_PATH guard is no longer lexical.**  Every entry is `readlink -e`
resolved before the containment test, empty entries (which mean the implicit
current directory) and relative entries are refused, and an entry that resolves
outside the private closure or the one allowed toolchain is refused by name and
by target.  A symlink under the private root pointing at a shared mutable build
tree passes any string test and would have imported foreign bytes; it does not
pass this one.

**The live-importer guard now uses possession, not argv.**  Its first version
grepped `ps -eo args` for the private root, which is wrong in both directions: it
misses a reader that found the root through `LEAN_PATH` or its working directory
while its argv is relative, and it refuses an unrelated waiter whose command line
merely names the path.  It now looks for an **open file descriptor** under the
root, a **mapped file**, `LEAN_PATH`/`D3_LEAN_ROOT`/`LAKE_*` in the
**environment**, or a **working directory** inside it — every candidate resolved
before the containment test — and excludes itself and its own ancestors.  Run
against the live TV re-attempt it refuses with exit 4 and says *why* for each:
`cwd inside the root` ×2, `D3_LEAN_ROOT references the root`, and `open fd ->
…/logs/issue_stage_gate.time` ×2.

**None of this retracts the observed passes.**  The frontend and issue-stage
eight-cycle readiness runs, and the two TV `agree` verdicts, were produced by
real runs whose raw records are retained; the counterexample is about what the
dispatcher would credit in future, not about those.

### 63.13 Identity now binds bytes, and the importer guard's ownership logic was backwards

**Two more synthetic counterexamples, both real.**  `IDENTITY.txt` bound the lean
binary by *path and version* and the search path by a *hash of the LEAN_PATH
string*.  Neither binds content, so:

* replacing the lean **executable's bytes** at the same path, with `--version`
  still agreeing, left the identity **byte-identical** — a different compiler
  could have been reused as the same one;
* editing a **dependency `.olean`** at the same LEAN_PATH left the identity
  **byte-identical** — different imported bytes could have been reused as the
  same.

The identity now carries: `lean_bin_sha256` (the executable's bytes);
`lean_path_closure_sha256`, a digest over **every `.olean` under the private-root
LEAN_PATH entries** — measured 8,651 files / 1,909 MiB in **14.7 s**, computed
once per invocation and reused by every phase, which is dependency accounting and
belongs to no generation or readiness metric; `import_direct` lines giving each
resolved `.olean` that `lean --deps` says the driver actually imports, **with its
content hash**; the **normalized resolved** LEAN_PATH entries with an existence
flag, rather than only validating them for containment; and the **exact argv as
an array**, one element per line, so one argument containing a space is not two
arguments.

`readlink -e` became `readlink -m` for the containment test: `lake env` legitimately
emits a LEAN_PATH entry for a package with nothing built (the `Cli` package here),
and refusing that is wrong — while `-m` still resolves symlinks in the components
that exist, so a symlinked entry pointing at a shared mutable tree is still caught.

Eleven fixture checks now pass, scratch **project-local** (the default was a
root-level `mktemp -d`, which violates the standing instruction), and
`retained_unchanged` is YES in every one.

**The importer guard's ownership logic was backwards.**  `mine(pid)` walked each
**candidate's** ancestors looking for the guard's pid.  The comment claimed it
excluded "my own ancestors"; it actually excluded every **descendant** of the
invoking shell — so a real importer launched from the same session would have
been silently skipped.  The exclusion is now exactly two pids: this shell and the
inspection helper.  Nothing is excluded for being a descendant.

Also fixed: an `fd` target of `pipe:[N]` or `socket:[N]` is not a filesystem
path, and joining it onto a cwd inside the root made **every pipe a false hit**.
Non-`/` targets are skipped.

Added: `PCB_KNOWN_CONSUMERS="pid:starttime_ticks,…"` checks named handles by
**identity**, so a recycled pid is reported as recycled rather than mistaken for
the consumer; and a process that exists but cannot be inspected is reported
**UNINSPECTABLE and refuses**, acknowledgeable only by naming its pid *and*
starttime.  On this host that is `systemd --user` (pid 20340).

Guard fixture, four spawned processes with known properties, each killed by pid
and no `pgrep` pattern anywhere:

| process | property | expected | observed |
|---|---|---|---|
| A | open fd inside the root, cwd outside | detected | detected by fd |
| B | `LEAN_PATH` in the environment, **relative argv** | detected | detected by environment |
| C | `mmap` of a file inside the root | detected | detected by fd (the mapping holds one) |
| D | unrelated waiter whose **argv names the root**, holding nothing | absent | absent |

B and D are precisely the two the argv grep got wrong in opposite directions, and
all four are descendants of the invoking shell — which the ancestor walk would
have excluded wholesale, reporting a clean context with three live readers.

### 63.14 The initial sweep is terminal, and the 14-target closure exposed a stale artifact

**`issue_stage_gate` translation validation reached `agree`** at the larger
budget: all seven gates 1/1, probe wall 4391.42 s, run wall 4572.56 s, maxrss
8,208,144 KiB = 7.83 GiB, exit 0.  The 1800 s cap was simply too small — the row
needed 4391 s — which is what "a cap is undecided, not a capability refusal"
means in practice.  The capped attempt is retained untouched.  Every one of the
60 cells now has a real attempt or a precise prerequisite disposition.

**The owed 14-target prerequisite is discharged, and it was not a formality.**
`lake build --no-cache` over the fourteen named `Compiler` targets ran **8,576
jobs** — against the **7** the default target covers — exit 0, wall 261.55 s,
user 120.16, sys 61.78, maxrss 6,953,728 KiB = 6.63 GiB; the explicit 14-target
`lake --no-build build` then exited **0** (wall 14.51 s).  That is the
validation; neither default-target success nor artifact hash equality discharges
it, as this run shows.

**Two of the fourteen artifacts changed, and the two changes are NOT the same
kind of thing.**

* `CompileDesign` 511,120 → 511,184 bytes.  The difference found is the
  **embedded absolute source path**: the old object carries
  `…/livehd-pass-lean-master/formal/lean/…/CompileDesign.lean`, the new one the
  private checkout's path, one occurrence each, and the size delta is exactly
  that one string's growth (64 bytes).
* `ReifyProof` 13,904,184 → 13,993,824 bytes, **+89,640** — which one path
  cannot explain, and does not.  Its live-tree `.olean` was built
  **2026-10-10 13:11:21** while `ReifyProof.lean` was last changed
  **2026-10-11 00:04:47** (commit `7914776a5`, *"d3: a search budget is not a
  refusal; the transport repair"*).  The artifact was **stale with respect to
  its own source by about eleven hours**, it was copied into the private root in
  that state, and the three TV runs loaded it.

So my earlier reading — that the out-of-date verdict was a path-keying artifact
of copying — was right for `CompileDesign` and **wrong for `ReifyProof`.**  The
explicit check is what distinguishes them, and that is precisely why it is owed.

**Which prior results actually depended on the changed artifacts**, established
rather than assumed.  The transitive import closure of the four FP drivers
contains `CompileDesign`, `CompileGraph`, `CompileOp`, `Runtime`, `DesignCert`,
`ResidualIR`, `ResidualSemantics` and the three `*Defs` — and **not**
`ReifyProof`, `Reify`, `ReifyGen` or `D3Harness`.  Therefore:

* the **FP** results are unaffected by the semantic change: they never load
  `ReifyProof`, and the only `CompileDesign` difference is the embedded path;
* the **TV** results loaded the stale `ReifyProof`, which is in `CRITICAL_OLEANS`
  and is the D3 reifier/proof module.  They stand as **historical observations
  under the closure they actually loaded** and are not evidence about the
  validated closure;
* the **IR-as-semantics** results used the live tree's `LEAN_PATH`, not the
  private root, so the private rebuild does not reach them;
* **legacy** and **verified compiler** are C++ `lhd` emission with no Lean
  closure at all.

The three TV cells are therefore `RUNNING` with their historical records
retained, and the affected fresh evidence — and only that — is being obtained:
one sweep over the same three certificates at `--timeout 7200`, against the
freshly validated closure.

### 63.15 Fresh closure results reconciled after overnight completion

Audited on 2026-10-12 from retained producer logs, GNU time exit records,
certificate hashes and the results hash in the run metadata; no benchmark rerun.
The three-target sweep `tv12x5/run_fresh14_20261011-234035` completed at
2026-10-12 01:16:29 PDT, producer exit 0. All three targets passed all seven
generation/admission gates with 32 distinct sampled observations agreeing.

| Target | Combined probe wall (s) | User / system CPU (s) | Max RSS (KiB) | Verdict |
| --- | ---: | ---: | ---: | --- |
| picorv32 | 71.88 | 62.98 / 3.34 | 2,407,192 | agree |
| frontend_gate | 1369.71 | 1347.42 / 25.96 | 8,167,072 | agree |
| issue_stage_gate | 4301.77 | 4109.33 / 56.21 | 8,203,020 | agree |

The validated artifact digest is
`705ffd6c754bc22a85447d51acd372d2178d4db04f2d3cb9c54dafe1b34cf2be`;
the result digest is
`44980dafa44d00da9d0a9182a01d9edf3989953c5e4c47d1b2f7e8951c7272ac`.
Both explicit private build exit records remain 0. Earlier stale-closure results
and the 1800-second undecided cap are retained as historical attempts.
These are interpreted generation/admission results; proof was not requested.
Probe timing excludes upstream RTL/emission and generic dependency preparation.

JSON/TSV/HTML now record **12 YES, 11 NO, 37 BLOCKED, zero RUNNING or PENDING**.
Every cell has an attempted or prerequisite disposition; 12/60 successful cells
is not full support. The next stage is plan sections 15.2-15.3: run matching
whole units on the old FP implementation, compare with master, and repair needed
frontend/projection support before IR-as-semantics and translation validation.
The untracked FP support manifest is source inspection, not old-branch execution.
This reconciliation is provisional and does not discharge the independent
migration, proof, validation or final all-60-YES gates.

### 63.15 Corrections, and the first real piece of FP memory support

**Two of my own claims are withdrawn.**

*"The old branch is not built."*  That conflated two components.  The old FP
**specializer** was built and run: `generated/master_port/fp/genprobe/root-pin/
nat/gen_probe`, sha256 `eeada8cf4405dc36…`, and §56 records 33 frozen rows run
matched against master — 25 `GENERATION OK`, 8 refused, 0 failures, 0 fuel stops.
What has not been built is the old repository's native RTL **frontend** (`lhd`),
which is a different component.

*"no-old-route" for five units.*  Absence of a convenience wrapper or filelist
script does not show the generic old `lhd` reader cannot consume explicitly
supplied sources.  The old stress runner already accepts `CVA6_FILELIST` and
`CVA6_WRAPPER_FILE`, and wrappers and filelists are benchmark **input data**, not
branch implementation.  Those rows are relabelled `helpers-absent`: the generic
route is **unattempted**, not unavailable.  B11's matching TLB refusal likewise
comes from the third-repo census and is **not** a matched old-FP-frontend
execution; it supports an inherited *source-gap* claim and nothing more.

**Implementation: `Projection/MemLog.lean`.**  PicoRV32 is the one requested unit
whose FP route actually reaches the specializer and is refused there, so it is
where FP implementation can start without any frontend work.  Its refusal is three
distinct gaps — `memFree` (1 memory, aw 5, dw 32), `sources` (`memImg(idx 0, aw 5,
dw 32)`), `ops` (`Op_MemRead` ×2, `Op_MemWriteBE 1` ×1).

`memFree` is not a gate that could be deleted; it is load-bearing for a
**representation** reason.  `RuntimeState.mems : Array (Int → BV)` holds memories
as *functions*, `Val` is first-order, and `decState` therefore rebuilds
`mems := #[]` unconditionally — `StateRel_memFree` states exactly that as a
theorem.  So the first missing piece is a memory image `Val` can carry whose
denotation is the reference's.

A memory reachable in a run is never an arbitrary `Int → BV`: it is a base image
plus the finite writes performed so far.  `MemLog` is that — a base index and an
override list, newest first, so last-write-wins is the definition rather than an
invariant.  The content is three equalities against the reference operators in
`Translation/LGraphModel.lean`, not resemblances:

| theorem | statement |
|---|---|
| `denote_writeEn` | `denote bs (writeEn m a d en) = cert_mem_write (denote bs m) a d en` |
| `denote_writeBE` | `denote bs (writeBE w bs m a d bev byte_w) = cert_mem_write_be w (denote bs m) a d bev byte_w` |
| `readEn_eq` | `readEn w bs m a en = cert_mem_read w (denote bs m) a en` |

plus last-write-wins, non-interference at other addresses, the `Val` round trip
and relational functionality.  The file elaborates **exit 0 in 1.78 s**, and its
axiom audit prints `propext` / `Quot.sound` only — Lean's own kernel axioms,
arriving through `funext` and `simp`.  What is **absent** is what matters: no
`sorryAx`, no `Classical.choice`, no `Lean.ofReduceBool`; `decide` is used for the
worked example rather than `native_decide` to keep the last of those absent.

Non-vacuity is checked at PicoRV32's real shape (`aw 5, dw 32`): a write of
`0xDEADBEEF` to address 7 reads back through `cert_mem_read`, a neighbouring
address is untouched, a disabled read is zero, a disabled write is a no-op on the
log itself, and the log holds exactly one override after one write.

**This does not admit PicoRV32, and nothing here pretends otherwise.**  `memFree`
is untouched, no memory is erased, no predicate is weakened, `RuntimeState` is
unchanged and nothing is yet wired into `I_hw`.  The remaining obligations are
enumerated in the file as `MEMORY_BRIDGE_OBLIGATIONS`: the `I_hw` source and
operator cases; a memory-carrying `encState`/`decState` with its `StateRel` chain
reproved; the `HardwareAdequacy` runtime relation generalised off `memFree`;
`Hw.checkResidual` accepting the new operator shapes with a sound bound; and
`SupportedByProjection` replacing `memFree` with the real conditions.  Those are
the next implementation steps, not a reason to stop.

### 63.16 The base image was still a host oracle; `MemImage.lean` removes it

**The correction.**  `MemLog` stored a base INDEX and its `denote`, `writeBE` and
`readEn` all took an external `Array (Int → BV)` of base functions, which
`encMemLog` never encoded.  So the representation was not self-contained: a
first-order projected `Program` cannot reach a host function sitting outside the
encoding.  Its three theorems are true *relative* write-log lemmas and they did
**not** discharge the encoding obligation; saying that obligation was complete
was wrong.  The file is retained with that stated at its head, not deleted.

**`Projection/MemImage.lean`** carries the contents themselves:

    MemImg := words : List BV   -- the actual contents, index = address
              dflt  : BV        -- every address outside [0, words.length)

`get` is total with no oracle and `encMemImg` encodes all of it.

**The reachable-state contract is stated, not assumed.**  Reads are total on both
sides and `readEn_eq` needs no hypothesis.  Writes agree only where the table has
the address: `denote_writeEn` and `denote_writeBE` carry `InRange`, and
`setAt_outOfRange` records that an out-of-range write is a **no-op**, never a
silent extension.  `zeroImg_inRange` proves every `aw`-bit address is in range of
a `2 ^ aw` table, so a read/write memory satisfies the contract by construction.
`ofMemConst` uses **`contents.size`**, not `2 ^ aw`, keeps the initial words, and
`ofMemConst_above` proves it reads the default above them.  No total first-order
round trip for arbitrary `Int → BV` is claimed, because none exists.

**The object language really evaluates it.**  `memProg` is a `Program` of six
functions built from the projection's own `Prim`s — `isNil`/`hd`/`tl`/`consP` for
the table walk, `eqI`/`ltI`/`addI`/`subI`/`mulI`/`divI` for addressing, `bvBit`
and `bvMk` for the byte mask.  Nothing in it calls `cert_mem_read` or
`MemImg.get`.  Ten `#guard`s run it over `encMemImg`-encoded data at PicoRV32's
real shape (`aw 5, dw 32`): read of a zeroed file; a full masked write of
`0xDEADBEEF` at address 7 read back through the object language; **partial** byte
masks (byte 0 → `0x000000FF`, byte 2 → `0x00FF0000`); a disabled write leaving the
encoded image untouched; a neighbouring address undisturbed; the ROM reading its
**initialised** word inside its contents and the default above them; and the
object-language write producing exactly `encMemImg` of the **host** `writeBE`
result, on both the full and the partial-mask case.

I checked that those guards bite rather than assuming it: mutating the
partial-mask expectation from `0x000000FF` to `0xFFFFFFFF` makes the build fail
with *"did not evaluate to `true`"*, while the unmutated file has zero errors.
(My first attempt at that mutation test was itself broken — `lean` refused the
file for being outside the root directory and compiled nothing, so its "no
errors" meant nothing.  The result above is from the corrected test.)

**What is NOT proved, stated plainly.**  A general
`evalFuel n memProg ρ (call 1 [encMemImg m, x]) = .value (ofBV (m.get x))` for all
`m`, `x` and sufficient `n` is **not** proved.  The obstacle is fuel indexing, not
the implementation: the recursive `call` runs at a smaller fuel than its caller,
so induction on the address does not line the hypothesis up, and the proof needs
fuel-monotone strong induction on `n`.  That is the next step, and it is in the
file's obligation list.

**Fuel is bounded statically and NOT by the syntax height.**  The table walk
costs the address and the mask costs the word width; both are fixed by the design
(`2 ^ aw` or `contents.size`, and `dw`) and known at specialization, never by
run-time data.  `memFuel` records the bound.  The existing `Hw.checkResidual`
height bound does **not** cover these traversals, and wiring this one in is an
open obligation — nothing here assumes the old bound suffices.

Both modules elaborate exit 0 (`MemLog` 1.59 s / 533,768 KiB, `MemImage` 1.84 s /
529,260 KiB).  PicoRV32 remains refused: `memFree` is untouched, no memory is
erased, no predicate weakened, `RuntimeState` unchanged, and nothing is wired into
`I_hw` yet.

### 63.17 Memory-aware state codec; and a fuel proof attempted and not landed

**Correction to my own fuel claim.**  I wrote that the table walk's cost is "the
ADDRESS, fixed by the design".  That is wrong twice: the address is **run-time
data** and is fixed by nothing, and the walk does not need it to be — `getGo`
stops at `nil`, so a negative or huge address terminates there and returns the
default.  The bound is the **table LENGTH**, which *is* design-determined
(`2 ^ aw`, or `contents.size`).  `memFuel` is renamed to take the length and is
labelled in the file as a **recorded proposal, not a theorem**.

**Correction on `InRange`.**  `setAt_outOfRange` says *our table update* is a
no-op out of range.  The **reference** write is not: `cert_mem_write` at an
address the table lacks does take effect on the function-valued memory.  That is
exactly why `InRange` is a real premise of `denote_writeEn`/`denote_writeBE` and
must be **established** for a supported source/state/operand path, never assumed
from a label.  `zeroImg_inRange` establishes it for a `2 ^ aw` read/write table;
a ROM is read-only, so no write premise arises there.  The file now says this.

**New: the memory-aware state codec.**  `RuntimeState.mems : Array (Int → BV)` is
not first-order, so `StateM` is the representable counterpart — the same flops,
one `MemImg` per memory — with `denote` mapping it onto the reference state by a
**function**, not an assumption:

| theorem | content |
|---|---|
| `decStateM_encStateM` | round trip: flops **and** every memory image survive |
| `StateRelM_functional` | one value denotes at most one state |
| `StateRelM_denote_functional` | …and pins the reference state a runtime relation needs |
| `denote_memFree` | `MemFree (denote s) ↔ s.mems = #[]` — the **memory-free compatibility** result |

`denote_memFree` is what makes the existing memory-free path a *special case*
rather than something replaced: `encState`/`decState`/`StateRel` and their
theorems are untouched.  A `#guard` round-trips a memory-bearing state carrying a
**ROM's initial contents** — the case `decState` cannot represent at all.

Axioms across all ten audited theorems: `propext` and `Quot.sound` only.  No
`sorryAx`, no `Classical.choice`, no `Lean.ofReduceBool`.  File elaborates exit 0
in 1.43 s, 535,680 KiB.

**The general evaluator bridge was attempted and did NOT land.**  I restructured
`getGo_sound` the right way — about the function **body** with a value
environment, because the recursive `call`'s arguments are computed terms and an
induction hypothesis phrased about a `call` with literal arguments does not apply
to it — and the repository already has `evalFuel_mono`, which is the other half.
The induction on the list and the fuel arithmetic (`4·len + 8`, three fuel per
step) are right.  What did not land is the tactic plumbing: `simp` does not drive
the nested `evalFuel`/`evalFuelList` match reduction to completion in the `cons`
case, and the index lemma relating `(a :: t)[x.toNat]?` to `t[(x-1).toNat]?`
needs more care than a one-line `simp`.  Rather than commit a file that does not
compile, the attempt is retained verbatim at
`generated/memlog/attempts/MemImage.getGo_sound.attempt.lean` and the committed
module is the one that builds.  This is a proof-engineering debt with a known
shape, not a blocked capability and not a request for a decision.

Still open and unchanged: the `I_hw` `memImg`/`memConst` source cases and the
`Op_MemRead`/`Op_MemWriteBE` operator paths; the clock-controlled commit and
reset/async metadata carried through; `Hw.checkResidual`'s bound for these
traversals; and `SupportedByProjection` replacing `memFree` with the real
conditions.  PicoRV32 stays **BLOCKED**: nothing is wired into `I_hw`, no
predicate is weakened, no memory or metadata erased.

### 63.18 The general evaluator bridge for the lookup path is proved

**It landed.**  `getGo_body` and `getGo_sound` hold for **every list and every
run-time address** — a negative address, an address inside the table and an
address past the end all covered, which is what the `if 0 ≤ x` in the statement
records.

**Their bounds are NOT the same**, and my first summary wrongly implied they
were: `getGo_body` needs `4 · length + 8`, the literal-argument `getGo_sound`
needs `4 · length + 9`.  Neither of them covers the encoded-image wrapper, which
is a separate theorem (§63.19).

The fix was to stop asking one broad `simp` to drive the whole six-function
program, exactly as the audit suggested.  The proof is now a chain of rewrites
over small one-step lemmas — `eval_lit_succ`, `eval_var_succ`,
`evalList_cons_eq`, `eval_prim_succ`, `eval_ite_true_succ`,
`eval_ite_false_succ`, `eval_call_succ` — with the list-index step factored into
`cons_index_step` (split `x = 0` / `x > 0` / `x < 0`, integer and `toNat`
arithmetic discharged by `omega`), and the induction on the **list**.  Fuel
levels are written as explicit successors at every site, which is what the
earlier attempt got wrong.

**The audit's measurements matched the proof.**  Minimum observed body fuel for a
far-outside address was 3, 6, 9, 27, 99 at lengths 0, 1, 2, 8, 32 — three levels
per list step, i.e. `3·len + 3`.  The proved bound `4·len + 8` sits above that
with slack, and three `#guard`s now exercise the **proved** bound at PicoRV32's
table size: 137 fuel suffices for an in-range address, the far end, and an
address past it.

**Axioms, reported as they are.**  `cons_index_step` and the eleven earlier
theorems use `propext` / `Quot.sound` only.  `getGo_body` and `getGo_sound`
additionally use **`Classical.choice`**, which enters through `simp`/`omega`'s
classical reasoning.  It is one of Lean's own three axioms, not an addition to
this development's trusted base, and I am recording it rather than rounding it
down: no `sorryAx` and no `Lean.ofReduceBool` anywhere.  File elaborates exit 0
in 5.04 s, 667,596 KiB.

**Still not proved, and named:** the `set`/`setGo` update and the
`maskGo`/`writeBE` byte mask have **no general correspondence theorem** — only
the executed `#guard`s.  The same lemma kit applies to them and they are next.
Then the `I_hw` `memImg`/`memConst` source cases and the
`Op_MemRead`/`Op_MemWriteBE` operator paths, which also need the function-index
question answered: `memProg`'s functions are numbered 0–5 and **cannot be
appended to `I_hw` unchanged** — either the helper program is composed with an
index offset, or its calls are relocated.  After that: the memory-bearing result
and runtime relation, clock-controlled commit with reset/async metadata
preserved, a residual bound the checker accepts, and `SupportedByProjection`.

PicoRV32 stays **BLOCKED**.  An evaluator lemma is not admission, and nothing
here is wired into `I_hw`.

### 63.19 The encoded-image wrapper is proved, and a false header claim is fixed

**The header claimed a theorem that did not exist.**  `MemImage.lean`'s header
said *"`memGet_sound` proves the object-language lookup equals `MemImg.get` for
every image and address"*.  There was no `memGet_sound`.  The proved table walk
does **not** discharge that: the wrapper also has to unpack the constructor, find
the default word and do the sign test.  The claim is now true because the theorem
exists.

`memGet_sound (m) (x) (ρ) (n) : 4 * m.words.length + 15 ≤ n →
evalFuel n memProg ρ (call 1 [encMemImg m, x]) = .value (ofBV (m.get x))` — for
**every image and every run-time address**, negative, in range, or past the
contents.  Same method as the walk: `eval_letIn_succ` joins the existing one-step
lemmas, each `letIn` is discharged with its own evaluated value, and the sign
test splits into the `get_neg` branch and the `getGo_body` branch.

**Three bounds, three different numbers**, now stated separately everywhere:

| theorem | covers | bound |
|---|---|---|
| `getGo_body` | the walk, value environment | `4·len + 8` |
| `getGo_sound` | the walk, literal arguments | `4·len + 9` |
| `memGet_sound` | the **encoded image** wrapper | `4·len + 15` |

Two `#guard`s exercise the proved **wrapper** bound at PicoRV32's table size
(143 fuel): a `zeroImg 5 32` read at address 31, and a ROM read returning its
**initialised** third word.  `memGet_sound` uses `propext`, `Classical.choice`
and `Quot.sound` — Lean's own axioms, through `simp`/`omega` — with no `sorryAx`
and no `Lean.ofReduceBool`.  File elaborates exit 0 in 5.92 s, 678,688 KiB.

**Still owed, unchanged and not softened:** general correspondence for
`setGo`/`set` and for `maskGo`/`writeBE` (only executed `#guard`s today); the
combined `memFuel` stays labelled **unproved** until its composed bound is
established; the enabled-write reference bridge still needs its real `InRange`,
width, shape and byte-width conditions discharged on the supported path — our
finite-table out-of-range no-op is **not** the reference function's write
semantics; the `I_hw` source and operator cases, which require the helper
function identity to be resolved properly (parameterised lookup, or composition
with a checked evaluation-preserving relocation — appending functions 0–5
unchanged is wrong); the memory-bearing **result** codec and a relation to the
real `RuntimeState`/`RuntimeResult` rather than only to `StateM`; clock-controlled
commit with reset/async metadata; and the residual/checker and adequacy premises.

PicoRV32 stays **BLOCKED**.

### 63.20 The update path is proved; the range counterexample is kept as a control; and `checkResidual` rules out the helper program

**`setGo_body` is proved** for every list, every run-time address and every fuel
at or above `5 · length + 14` — a looser bound than the walk's because the
rebuild nests one level deeper, and stated separately rather than folded into the
others.  `cons_set_step` carries the index arithmetic, with the same three-way
split: a negative address and an address past the end both rebuild the list
**unchanged**, which is also what `List.set` does past the end, and the
`if 0 ≤ x` is what stops a negative address being read as index 0.

**The range counterexample is now a retained control in the module.**  A 32-word
`aw = 5` image given a deliberately **8-bit** address 255: the reference
`cert_mem_write_be` writes there and reads `0xDEADBEEF` back, while the finite
table has no such address, so our write is a no-op and the read gives 0.  Two
`#guard`s pin both sides and a kernel-checked `example` proves they **differ**, so
the control cannot rot into a no-op.  A second `example` proves
`¬ InRange imgW (bv_uint wideAddr)` — the premise that excludes the shape is
decidable and false here, which is the whole point.  This is a correctly refused
**operand shape**, not a defect in the guarded lemmas and not a statement about a
correctly shaped whole core: `zeroImg_inRange` discharges `InRange` exactly when
the address width matches the table.

**A structural prerequisite I verified at source, not from the summary.**
`ResidualFragment.checkResidual` accepts `R.funs = [fd]` — **one** function —
with `R.entry = 0`, `fd.arity = 3` and `fragB 3 fd.body`; and `fragB` answers
**`false` on `.call _ _`**, so an accepted residual is entirely **call-free**.
Its soundness theorem excludes `.outOfFuel` at `height + 1` for arbitrary `Val`
arguments, while `.typeError` stays possible.

So the six-function `memProg` is **not admissible as a residual**, and it was
never going to be: `memProg` is the *interpreter* side, and the residual is what
`mixDriver` produces by specializing against a design.  Because a design's table
length is static, a specializer can unroll the walk into a call-free residual —
that is the "proved call-free construction" route, and whether `mix` actually
unrolls these recursive calls is now the concrete open question.  The alternative
is a proved checker extension with explicit sufficient-fuel and valid-shape
premises.  **What is not available** is reusing the call-free height theorem for
recursive helpers, or claiming the design-dependent table-length bound covers
arbitrary malformed values without a guard or a typed runtime premise.

Axioms: `cons_set_step` uses `propext`/`Quot.sound`; `setGo_body` adds
`Classical.choice` like its siblings.  No `sorryAx`, no `Lean.ofReduceBool`, and
no `sorry` anywhere in the file.  Exit 0 in 6.08 s, 753,912 KiB.

**Still owed:** `maskGo`/`writeBE` general correspondence and the composed
`memFuel` bound — `memFuel` stays labelled a **proposal**; the `I_hw` source and
operator cases with the function-identity bridge; the memory-bearing result codec
and a relation to the real `RuntimeResult`; clock-controlled commit with
reset/async metadata; and the checker/adequacy premises above.  PicoRV32 stays
**BLOCKED**.

### 63.21 An actual admission check, and the encoded update wrapper

**Correction: "a correctly refused operand shape" was wrong.**  Nothing refused
it.  The wide-address example merely **falsifies the `InRange` premise**, so the
guarded theorems never applied, and the unguarded helper **silently left the
table unchanged**.  A false premise is not a producer rejecting an input, and I
should not have used the word "refused" for it.

So the check now exists.  `writeAdmissible` is decidable;
`writeAdmissible_iff` proves it is exactly `InRange`; and `writeChecked` is the
real admission path — a disabled write is admitted as a no-op, an enabled write
is admitted **only** in range and answers `none` otherwise.
`writeChecked_sound` proves **every admitted write agrees with
`cert_mem_write_be`**, which is what makes it an admission check rather than a
filter.  The controls now exercise the check rather than the premise:

* `writeChecked 32 imgW wideAddr … = none` — **actually refused**, kernel-checked
* `(writeChecked 32 imgW (mk_bv 5 7) …).isSome` — the correctly shaped PicoRV32
  write is **admitted**, so the check is not merely conservative
* `writeChecked 32 imgW (mk_bv 5 7) … (bev := 0) … = some imgW` — a disabled
  write is admitted and is a no-op, as the reference says

and the earlier disagreement example is kept beside them, so the reason the shape
must be refused is still visible.

**`memSet_sound` is proved**: `call 3` on an `encMemImg` equals `encMemImg` of
`setAt`, for every image, address and value, at fuel ≥ `5 · length + 24`.  A
negative address returns the image untouched; an address past the contents
rebuilds it unchanged, which is `setAt`'s out-of-range no-op.  A `#guard`
exercises the proved bound at PicoRV32's table size (184 fuel): write `0xC0FFEE`
to word 7 and read it back, both through the object language.

**Five different bounds, all stated separately** — `4·len+8` (walk body),
`4·len+9` (walk, literal args), `4·len+15` (read wrapper), `5·len+14` (update
body), `5·len+24` (update wrapper).  `memFuel` remains labelled a **proposal**;
it is not any of these and is not yet a theorem.

Axioms: `writeAdmissible_iff` `propext`; `writeChecked_sound` `propext`/
`Quot.sound`; `memSet_sound` adds `Classical.choice`.  No `sorryAx`, no
`Lean.ofReduceBool`, no `sorry`.  Exit 0 in 7.03 s, 769,136 KiB.

**Still owed:** `maskGo`/`writeBE` general correspondence (the byte mask has no
general theorem yet, only executed `#guard`s) and the composed bound; the `I_hw`
source and operator cases with the function-identity bridge; the memory-bearing
result codec and a relation to the real `RuntimeResult`; clock-controlled commit
with reset/async metadata; and — the structural one from §63.20 — a call-free
residual construction or a proved checker extension, since `fragB` rejects every
`.call`.  PicoRV32 stays **BLOCKED**.

### 63.22 The memory-primitive route, costed at source — and what blocks it today

**Correction, one level up from the last one.**  I called the wide-address shape
"actually refused" because `writeChecked` answers `none`.  That is still wrong.
`writeChecked` is a **sound host helper**, not a producer: nothing in the FP
route — not the generator, not `supportReport`, not the checker — calls it.  A
helper returning `none` establishes a refusal only once it is wired in, and it is
not.  The module now says so at both places.

**I costed the primitive route at source rather than estimating it.**  Adding a
memory primitive to the object language touches, verifiably:

| site | file | size today |
|---|---|---|
| `Prim` + `Prim.arity` | `ObjectLanguage.lean:151,183` | 18 arity cases |
| `evalPrim` | `ObjectLanguageSemantics.lean:91` | 71 cases |
| `primCode` / `decPrim` | `Encoding.lean:70,102` | with `decPrim_encPrim` parity at :107 |
| **`primTable`** | **`MixProgram.lean:166`** | **32 entries, codes 0–31** |

The fourth is the one that matters and is easy to miss: `primTable` is the
**specializer's own object-language copy of `evalPrim`**, mapping primitive codes
to terms.  A new primitive that is not mirrored there cannot be specialized by
`mix` at all — so "add a primitive" is not a local change to the interpreter, it
is a change to the interpreter *and* to `mix`'s self-representation, with the
`decPrim_encPrim` round trip to re-prove.

**Both routes therefore have a verified blocker, and neither is hand-waving:**

* *memory primitive* — must be mirrored into `primTable` or specialization breaks;
* *helper functions called from `I_hw`* — the calls survive into the residual, and
  `fragB` answers `false` on every `.call` (§63.20), so `checkResidual` rejects it.

The second has a known escape (a specializer that unrolls the walk emits a
call-free residual) but §63.20's caution stands: the helper body takes a runtime
list and address and receives **no static length parameter**, so whether `mix`
actually unrolls it must be **tested on an emitted residual**, not inferred from
the design metadata being static.  That test is the next concrete action.

**What this checkpoint did not do:** it did not wire memory into `I_hw`, and no
integration landed.  I am not going to start a partial `Prim` addition that
leaves the development unbuildable; the next step is the specialization
experiment above, which decides which of the two routes is actually open.
PicoRV32 stays **BLOCKED**, and nothing in the 60-cell table changes.

### 63.23 The memory READ primitive is implemented, proved and validated on both mix paths

**Two of my claims are corrected first.**

*"Cannot be specialized at all."*  Too strong.  The independent control shows
that removing a `primTable` mirror makes **static** object-mix generation
`typeError`, while **dynamic** object-mix still generates the code and executes.
Static evaluation needs the mirror; dynamic residualisation is generic.

*"Helper calls survive into the residual."*  Not an observed result.  `fragB`
rejecting `.call` is a **conditional** limitation on what the checker accepts; it
says nothing about the structure of a residual nobody has generated.

**My costed blast radius was also wrong — four sites, actually six.**  The build
found the two I missed: `PartialEvaluatorCorrect.lean:1630` (`primStruct_ok`) and
`:3569` (`primStruct_sound`), both exhaustive `cases p with` splits **inside
proofs**.  I had grepped for `def` sites only.  `primStruct` returns `none` for
`memRead` through its catch-all, so it joins the alternation that already
discharges 28 other primitives with `simp [primStruct] at hps`.

**`Prim.memRead` is implemented across all six sites, nothing renumbered**
(`bvShl` is still 31, `memRead` is 32):

| site | change |
|---|---|
| `ObjectLanguage.lean` | the constructor, arity **4** |
| `ObjectLanguageSemantics.lean` | `memImgTag`, `valMemWalk`, `valMemRead`, the `evalPrim` case |
| `Encoding.lean` | `primCode = 32`, `primOfCode 32` |
| `MixProgram.lean` | the `primTable` mirror, plus a new `pD` operand accessor |
| `PartialEvaluatorCorrect.lean` ×2 | the `memRead` alternative in both proof splits |

The walker sits in `ObjectLanguageSemantics` on purpose — importing `MemImg`
there is a cycle — and works on the **encoded** form: a cons chain of `BV`s and a
default word, both inside the value.  No index into an external `Int → BV`, no
oracle.

**The reference bridge is proved**, all with `propext`/`Quot.sound` only:
`tagMemImg_eq_memImgTag`; `valMemWalk_eq` — the walker equals the image lookup
for every list and run-time address; `valMemRead_eq` and `evalPrim_memRead` — the
primitive **is** `cert_mem_read` on valid operands, enabled and disabled.

**Totality and encoding are checked, not assumed:** a malformed image, a wrong
tag, the wrong operand count and a non-`BV` address each give `.error`; the code
round trip holds and `bvShl` keeps 31.  Discriminating runs: a ROM reads its
**initialised** word, reads the default above its contents, a disabled read is
zero, and a `writeChecked` result is visible to the primitive.

**Both mix paths validated against the actual `PartialEvaluator` code**
(`Projection/MemPrimChecks.lean`): `.stat` folds through `evalPrim` to the ROM's
word; `.dyn` has no `primStruct` rule so mix residualises an opaque
`.prim .memRead` node; and that node **passes `fragB 4`** — no call, no
recursion, so unlike the six-function helper program a primitive application is
inside the checker's fragment.  `fragB` also rejects it at the wrong arity.  The
guards bite: flipping the dynamic expectation to `.isSome` fails the build.

Built in an independently owned context (`generated/memprim`, the repo's own
`build-opt.sh`): 38 oleans, probe linked, exit 0; `MemImage.lean` 8.40 s.

**What this does not do:** `I_hw` does not yet call the primitive, there is no
`Op_MemRead`/`Op_MemWriteBE` wiring, no memory-bearing result relation and no
clock-controlled commit.  `memFree` is untouched.  PicoRV32 stays **BLOCKED**.

### 63.24 A real malformed-image defect, fixed; and the mix-run claim attributed properly

**The defect, reproduced before it was fixed.**  An image with the right tag but a
non-`BV` word:

```
decMemImg (ctor 124 [cons (int 5) nil, ofBV 7])  =  none
evalPrim .memRead [32, <that image>, 0, enabled] =  .ok (BV 32 7)      -- BEFORE
```

`valMemWalk` reads `(asBV a).getD d`, so a malformed word was **silently read as
the default**: an image the decoder rejects still answered `.ok` with a
plausible word.  My "a malformed image is an error" claim was **false** for this
case — the checks I wrote covered a wrong tag, a wrong field count and a non-`BV`
address, and missed a non-`BV` **word**.

**Fixed by implementing the gate, not by softening the claim.**  `valAllBV`
requires every element of the chain to be a `BV` and the chain to be
nil-terminated, and `valMemRead` now demands it:

* `valMemRead_refuses_badWords` — **proved** for all `w`, `ws`, `dv`, `a`, `en`:
  a chain containing a non-`BV` gives `none` (`propext` only);
* `valAllBV_encListG` — encoded lists pass the gate, so valid images cost nothing
  and `valMemRead_eq` is unchanged (`propext` only);
* measured after:

```
decMemImg <that image>  =  none
evalPrim .memRead [...]  =  .error "memRead: malformed image or operand"   -- AFTER
evalPrim .memRead [ROM, 1, enabled] = .ok (BV 32 22)                       -- still right
```

The reproduced image is retained as a control in the module, so the gate cannot
regress silently.

**Scope correction on the mix claim.**  I wrote that both mix paths were
"validated against the actual `PartialEvaluator` code".  `MemPrimChecks.lean`
calls `evalPrim`, `primStruct` and `fragB` **directly**; it does **not** run
`mixTerm`, `mixDriver` or the object-level mix.  What it establishes is that the
components `mixTerm`'s `.prim` case dispatches to behave as required.  The file
header now says so.

The actual mix evidence is the **independent audit's**, not mine, and is recorded
as such: host `mixDriver` and object-language `mixProgram` run on static and
dynamic `memRead` programs, the generated programs agreeing; ROM output 22,
above-contents and disabled outputs 0; one arity-3 dynamic residual **accepted by
`checkResidual` at bound 3 and executed at bound 3**; exit 0, 27.85 s, 511,924
KiB; and the 22→23 mutation failing the guard with exit 1.  That last item is the
one that matters for integration: a residualised `memRead` node is inside the
checker's fragment, which the six-function helper program never was.

Chain rebuilt in the independently owned context: 0 errors, 39 oleans, probe
linked; `MemImage.lean` 22.93 s, 752,148 KiB, exit 0; `MemPrimChecks.lean` clean.

Not done: the byte-enabled **write** primitive, and `I_hw` still does not call
anything here.  `memFree` untouched.  PicoRV32 stays **BLOCKED**.

### 63.25 The byte-enabled WRITE primitive, with refusal inside the primitive

`Prim.memWriteBE` is implemented across the same six sites, **code 33, arity 6**,
with nothing renumbered (`memRead` 32, `bvShl` 31): the constructor and arity;
`valLen`, `valInRange`, `valMemSet`, `valMemWriteBE` and the `evalPrim` case;
`primCode`/`primOfCode`; the `primTable` mirror with new `pE`/`pF` accessors; and
the alternative in both `PartialEvaluatorCorrect` proof splits.

**The admission condition now lives inside the primitive**, which is the gap that
mattered.  `writeChecked` was a host helper nothing in the route called; the
primitive is evaluated by the backend itself:

* a **disabled** write is a no-op and is admitted;
* an **enabled** write is admitted only in range and answers **`none`** otherwise;
* the masked word is computed against the **current** contents at that address;
* the strict representation gate applies — a chain that is not entirely `BV`s is
  refused.

Proved, with `propext`(/`Quot.sound`) only and no `sorryAx` or
`Lean.ofReduceBool`:

| theorem | content |
|---|---|
| `valLen_encListG`, `valInRange_iff` | the encoded range check **is** `InRange` |
| `valMemSet_encListG` | the encoded update **is** the list update (via `cons_set_step`) |
| `valMemWriteBE_eq` | **the primitive is `cert_mem_write_be`** on an admitted operand |
| `valMemWriteBE_disabled` | a disabled write leaves the encoded image alone |
| `valMemWriteBE_refuses_outOfRange` | an enabled out-of-range write gives `none` — a **proved refusal**, not a false premise |
| `evalPrim_memWriteBE` | the evaluator case inherits all three |

**Discriminating controls, all through primitives:** full-width write then read
back; a **partial** byte mask giving `0x000000FF`; a neighbour untouched; a
disabled write leaving the encoded image identical; an enabled **out-of-range**
write (8-bit address 255 against a 32-word table) **refused by the primitive**;
and a ROM's initial contents surviving a write to a different word.  Plus arity,
code and round-trip checks with `memRead` still 32.

The refusal control bites: flipping it to expect `.ok` fails the build.

Chain rebuilt in the independently owned context: 0 errors, 39 oleans, probe
linked; `MemImage.lean` 8.83 s, 787,872 KiB, exit 0; `MemPrimChecks.lean` clean.

**Not done, and this is the remaining substance:** `I_hw` still does not call
either primitive.  No `memImg`/`memConst` source cases, no
`Op_MemRead`/`Op_MemWriteBE` operator cases, no memory-bearing result codec or
`RuntimeResult` relation, no clock-controlled commit and no inactive-clock case.
`memFree` is untouched and PicoRV32 stays **BLOCKED**.  Both primitives being
inside `fragB`'s fragment (§63.23) is what makes that integration reachable.

### 63.26 The hardware memory source and clock-controlled commit

**Scope note carried forward, as the audit asked.**  `valMemWriteBE_eq` gives the
encoded **host `writeBE`** update; the reference equality comes from
`denote_writeBE` **under `InRange`**.  The composed bridge is
`primitive → encoded writeBE → (InRange) → cert_mem_write_be`, and it is stated
that way rather than collapsed into one claim.

**`Projection.Hardware` implements the source and commit layer**, each matched to
its reference counterpart by a theorem rather than by resemblance:

| definition | theorem | reference it denotes |
|---|---|---|
| `absentImg` | `absentImg_denote` | the zero-width-zero image an absent memory reads |
| `srcMemImg` | `srcMemImg_denote` | `sourceValue .memImg` — the **state** image, absent ⇒ zero |
| `ofMemConst` | `ofMemConst_denote` | `sourceValue .memConst` — bound by **`contents.size`**, default 0, initial contents preserved |
| `memCommit` | `memCommit_denote` | `srcMemNext` — commit on `fires e m.clock`, otherwise hold the prior image |
| `stepMems` | `stepMems_quiet` | the per-memory array, each committed on **its own** clock |

`memCommit_inactive` and `stepMems_quiet` are the ones that matter for metadata:
with the clock quiet the prior image is returned **exactly**, not rebuilt, so
contents, defaults and sizes survive a step in which nothing fires.

**A discriminating state transition through this path**, not a standalone
primitive run — the image comes out of a `StateM` via `srcMemImg`, the write goes
through the `memWriteBE` **primitive**, the result is committed by `memCommit` on
the memory's own clock, and the read comes back through the `memRead` primitive
from the committed state:

* **active** clock → the write commits and the read sees `0xDEADBEEF`;
* **inactive** clock → the same write does **not** commit, the read sees 0, and
  `st'.mems` is identical to the prior array image-for-image;
* a **partial** mask at PicoRV32's 1-bit-per-byte enable → `0x000000FF`;
* a neighbour untouched;
* an enabled **out-of-range** write refused by the primitive, so no step happens;
* a ROM's initial contents surviving the **state codec** round trip.

The inactive-clock control bites: expecting `0xDEADBEEF` there fails the build.

Axioms: `ofMemConst_denote` `propext`; the other three add `Quot.sound`.  No
`sorryAx`, no `Lean.ofReduceBool`.  `MemImage.lean` 8.48 s, 827,188 KiB, exit 0
against the independently built chain.

**What is still missing, precisely.**  This is the memory layer `I_hw` needs, and
it is proved — but `I_hw` itself is not yet rewired: `SourceSupported` still
answers `false` for `memImg`/`memConst`, `OpSupported` still excludes
`Op_MemRead`/`Op_MemWriteBE`, and `supportReport.memFree` is untouched.  There is
no memory-bearing `RuntimeResult` relation yet and no generic step/adequacy
theorem over it.  PicoRV32 stays **BLOCKED**; these controls are not a YES.

### 63.27 Two mislabels corrected, a bit-enable control added, and B01's old route has now run

**Correction 1 — the mask control was labelled wrong.**  I called it "PicoRV32's
1-bit-per-byte enable".  It is not: `stepOnce` passed `.int 8`, so each enable
bit covered an **8-bit lane**.  PicoRV32's emitted operator is
`Op_MemWriteBE 1` — one enable bit per **data bit**.  The byte-lane case is valid
and is kept, now labelled as such, and `stepOnce` takes `byte_w` as a parameter
so the real shape can be exercised.

The **bit-enable** case is now there: `byte_w = 1`, mask `8` (bit 3 only) over a
zero word must leave exactly **8** — not `0xFF`, not `0xFFFFFFFF`.  That is the
case which discriminates bit-enable from byte-lane, and it passes.  Its mutation
(expecting `0xFF`) fails the build.  An inactive clock at `byte_w = 1` still
commits nothing.

**Correction 2 — scope of §63.26.**  Everything in `Projection.Hardware`,
including `stepOnce`, is a **HOST Lean function**.  `I_hw`'s encoded source
dispatch, its operator dispatch and its state/result shape are **unchanged**.
The commit title said "implement the hardware memory source and commit", which
reads as more than was done; the module now states the scope in its own header.
`stepMems_quiet` is likewise a **quiet-branch** lemma — not an unconditional
`stepMems`/`srcMemNext` preservation theorem, whose active branch needs the
actual `rho m.nextImg` correspondence that is not established.

**B01: the old generic route has now been executed, and the old/master claims
change accordingly.**  The supervisor's 9bb frontend build is terminal (exit 0,
3992.44 s, 2,500,512 KiB) and the **whole `minion_dcache_top` compile on the
frozen 40-source input PASSED**: exit 0, 8.79 s, 356,016 KiB, top-level JSON
pass, `graph_io 198087696323 minion_dcache_top`.

Fresh **master** on the **same** frozen RTL, filelist, top and reader options
reproduces the concat-lane-width refusal at `mux_94508`: exit 1, 6.14 s, 327,140
KiB, in the **second** `pass.bitwidth`, after
`import/cprop/bitwidth/legalize/enableopt/cprop`.  Old default O1 is
`import/cprop/formal`; master's O1 has a **different pass sequence**, and
reader/plugin/importer versions differ.

So B01 is a **real whole-frontend behaviour difference** — old passes, master
refuses, same input.  It is **not** yet a localised code regression, and it is
**not** old FP admission: a passing frontend compile is not a certificate, and
the pass sequences and reader versions differ.  The manifest is updated, my
earlier `unknown`/helpers-absent framing for B01 is superseded, and the next
frontend step is to trace the first semantic/width divergence **without**
disabling the Concat, `single_edge` or support checks.

**Still not done:** the `I_hw` rewiring itself — encoded memory-carrying state
and result in the interpreter, `memImg`/`memConst` through source evaluation,
`Op_MemRead`/`Op_MemWriteBE` through the real operator cases, per-memory
next-image selection inside the emitted interpreter.  `SourceSupported`,
`OpSupported` and `memFree` are untouched, deliberately: broadening them ahead of
the soundness bridge is the one thing that must not happen.  PicoRV32 stays
**BLOCKED**.

### 63.28 Actual object-language memory dispatch, executed through the resolved hardware program

**`Projection/HardwareMem.lean`** adds the memory source and operator dispatch to
the **same backend**, in its own module so the original memory-free `hwS` and
every theorem about it stay byte-identical.  `hwMemS` is built **from** `hwS` by
*adding* four functions — no existing function is edited — and the whole program
is resolved afresh by `resolveProgram`, so no pre-resolved numeric id is reused.
Measured: 45 functions, `memAt` 41, `romWords` 42, `srcValM` 43, `applyOpM` 44.

* `srcValM` answers the two memory source forms and **delegates** every other
  form to the original `srcVal`:
  - `memImg i _ _` → the i-th encoded image, or the **absent** image (`[]`,
    zero-width zero) when the list is shorter — matching
    `s.mems[idx]?.getD (fun _ => mk_bv 0 0)`;
  - `memConst _ dw ct` → first-order initialised words from `ct` with a zero
    default, so the word count is **`contents.size`**, not `2 ^ aw`.
* `applyOpM` dispatches the real opcodes **26** (`Op_MemRead`) and **28**
  (`Op_MemWriteBE`) to the object primitives, reading operands in certificate
  dependency order through `slot`, with the node width and the **payload**
  (`byte_w`) from the certificate; every other opcode **delegates** to the
  original `applyOp`, so the scalar cases and the SHL/SRA/Sext work behind them
  are untouched.

**Executed through the resolved program**, not a host wrapper: every check calls
`evalFuel hwMemP` at the ids `resolveProgram` assigned.  Thirteen pass —

* `memImg 0` → the state's image; `memImg 3` against a one-image list → the
  **absent** image exactly;
* `memConst` → `contents.size` initialised words, read back as 33 at index 2;
* `Op_MemRead` on a ROM → the **initialised** word; a disabled read → 0;
* `Op_MemWriteBE` payload **`byte_w = 1`** (PicoRV32's real shape), mask 8 →
  stored word exactly **8**, read back through the same dispatch;
* `byte_w = 8`, mask 1 → **`0x000000FF`** — the byte-lane case, distinct from the
  bit-enable one above;
* an enabled **out-of-range** write → `typeError` from inside the dispatch;
* a **malformed** image → `typeError`;
* a non-memory source still reaching the original `srcVal`.

The `byte_w = 1` control bites: expecting `0xFF` there fails the build.

**Staged, and not claimed.**  `main` and `mkSources` are **not** rewired: the
memory-bearing state/result encoding and the per-memory clock-controlled commit
inside `main` are the next increment.  This module is **source and operator
dispatch only** — no whole-step, adequacy or support credit follows from it.
`SourceSupported`, `OpSupported` and `memFree` are untouched and the public route
still refuses memory-bearing designs.  No `sorry`, no new axioms, no disabled
checks.

**B01's two distinct old-side phases, recorded separately** (neither is old FP
support):

1. the old **frontend** passes the whole `minion_dcache_top` compile — exit 0,
   8.79 s, `graph_io 198087696323` — while master refuses at concat-lane-width
   `mux_94508` in its second `pass.bitwidth`, on the same frozen input;
2. old **`single_edge`** on a privately copied emitted whole graph then **refuses**
   — exit 7, 0.15 s, 62,668 KiB: `gen_offload_reduce.u_reduce.u_ba_alloc_fifo.mem`
   would commit every phase-divider sub-step, slot enables not wired into Memory.

So the old route gets *further* than master and then stops for a different
reason.  Neither phase yields a certificate.

**B03 is a shared refusal, not a master regression.**  Old `9bb` on the frozen
31-source `icache_top` input refuses with the **same first ambiguity** as master —
`icache_pkg.sv:21:50`, `PaSize` imported from both `etlink_pkg` and `minion_pkg`
— exit 1, 0.13 s.  That is an actual shared raw-source/configuration refusal; the
fix is a proper input/configuration adapter with the qualification documented, not
silencing the ambiguity check.

### 63.29 Phase 1: the memory-aware whole-cycle entry, matched against the reference scenario

**`mainM` is the reachable entry.**  `hwMemS.entry = "mainM"`, resolved to id 50
of 51 functions — not the memory-free `main` with a memory-aware label on it.
Everything is resolved afresh; no pre-resolved id is reused.

* `mkSourcesM` (45) calls `srcValM` and **threads the memory-image list**;
* `evalNodesM`/`evalNodeM` call `applyOpM`;
* `memNext` (48)/`memNexts` build the per-memory next state: `m.nextImg`'s slot
  when `firesAt edges m.clock`, the **exact prior image** when quiet;
* `mainM` consumes `encDesign`, `encEdges`, `encInput` and the full `StateM`
  encoding, and feeds the environment to outputs, flop next-state **and** memory
  next-state.

The scalar and flop helpers — `lenL`, `nthD`, `slot`, `nz`, `mkOutputs`,
`flopNexts`, `firesAt`, `capture`, every operator case — are **reused unchanged**,
so the SHL/SRA/Sext work and the clock/reset/async logic are the same code the
memory-free route runs.  The original `hwS` and its proof chain are untouched.

**The memory-bearing result codec relates to the REAL `RuntimeResult`**, not just
`StateM`: `ResultM.denote : ResultM → RuntimeResult`, with
`decResultM_encResultM`, `ResultRelM_functional`,
`ResultRelM_denote_functional` (which pins the reference result an adequacy
statement needs) and `ResultM_denote_memFree` (the memory-free case is a special
case here too).

**Executed through `mainM` and compared with the independently checked reference
scenario** — two mutable 32-word memories on **distinct** clock domains, memory 0
on clock 1 and memory 1 on clock 0, `Op_MemWriteBE 1` bit-enabled writes, a
before-write read and an initialised ROM.  All thirteen agree with the reference:

| stimulus | reference | `mainM` |
|---|---|---|
| clock-1 only, mask 8 | words 8 / 22 | **8 / 22** |
| clock-0 only, mask 8 | words 0 / 30 | **0 / 30** |
| both quiet | words 0 / 22 | **0 / 22**, and the whole image array identical to the prior |
| clock-1, full enable | `0xDEADBEEF` | **`0xDEADBEEF`** |
| clock-1, disabled write | contents preserved | **preserved** |
| outputs, clock-1 | `[0, 22, 33]` | **`[0, 22, 33]`** |
| second step from the **decoded returned state** | `[8, 22, 33]` | **`[8, 22, 33]`** |

The inactive-domain control bites: expecting 8 for the quiet memory under
clock-0-only fails the build.

**Explicit shape premise, not assumed away.**  A memory slot carries an encoded
image and a scalar slot a `BV`; `mkOutputs` coerces with the scalar reading, so
an output whose slot named a **memory** would be mis-read.  That is recorded as a
shape premise on admitted designs rather than silently relied on — and no design
is admitted yet.

**Still owed:** the source/operator/commit preservation proofs against
`interpretDesign` with reachable shape/range/width premises; generic adequacy;
BTA/specialization and residual checker integration for this entry (the
production `checkResidual` wants arity 3 with edges/input/state dynamic, which is
what `mainM` has); and real support/driver wiring.  `SourceSupported`,
`OpSupported` and `memFree` stay untouched — support remains refused.  No
`sorry`, no axioms, no disabled checks.  PicoRV32 stays **BLOCKED**: these are
entry tests, not admission, equivalence or kernel certification.

### 63.30 Phase 2 begins: the runtime denotation relation to the REAL reference

**Scope correction, and it was a real gap.**  `StateRelM v s` has `s : StateM`
and says only `decStateM v = some s`; `ResultRelM` is the same shape.  Those pin
the decoded **witness**, not the independent reference value, so
`ResultRelM_denote_functional` was **not** the bridge an adequacy statement
needs, and describing it as a relation "to the real `RuntimeResult`" overstated
it.  The relations that are now defined and proved:

```
StateRelR  v s  :=  ∃ sm, decStateM  v = some sm ∧ StateM.denote  sm = s
ResultRelR v r  :=  ∃ rm, decResultM v = some rm ∧ ResultM.denote rm = r
```

— a representable witness **plus equality of its denotation** to the real
`RuntimeState`/`RuntimeResult`.

| theorem | content |
|---|---|
| `StateRelR_encStateM`, `ResultRelR_encResultM` | round trip: an encoded state/result denotes exactly what it came from |
| `StateRelR_functional`, `ResultRelR_functional` | **one value denotes at most one reference value** — the property adequacy needs, derived from the codec rather than postulated |
| `StateRelR_memFree` | a value denoting a `MemFree` reference state has no images in its witness, so the memory-free path is a special case of this relation |

**No finite codec for an arbitrary `Int → BV` state is claimed.**
`Representable s := ∃ sm, StateM.denote sm = s` is exactly the condition under
which one exists, and it is carried as a **premise**, never assumed.

**Canonicality is deferred, and said so in the file**: proving that a related
value *is* its witness's encoding needs a `Canonical encMemImg decMemImg` lemma
that does not exist yet.  It is listed as an obligation rather than asserted.

**Axiom reporting, corrected.**  I have been writing "no axioms", which is loose.
Precisely: the new codec and relation lemmas depend on `propext` and, where
`funext`/`simp` are involved, `Quot.sound` — Lean's own axioms.  What is absent,
and what I actually check for, is any **new unsafe** axiom: no `sorryAx` and no
`Lean.ofReduceBool` anywhere in the module (verified, 0 matches).  The whole-cycle
checks still pass unchanged.

**Still owed on this chain**, and the next code rather than a list: object-level
`memAt`/`romWords`/`srcValM` preservation via `evalFuel` on the resolved program
(the same explicit-lemma method that worked for `getGo_body`/`memGet_sound`);
`applyOpM` preservation through the existing
primitive → encoded host update → `denote_writeBE` bridge, discharging `InRange`
and the width/address/byte-mask conditions; a typed slot/value relation through
`mkSourcesM`/`evalNodesM`; `mkOutputs`/`flopNexts`/`memNext` on active **and**
quiet domains; then generic `mainM` adequacy against `interpretDesign`.

**The slot/value kind policy is still prose and must not stay that way.**  A
memory-valued output or operand cannot be silently coerced; the admission policy
has to be defined and checked, together with mutable image shapes, initial memory
contents, ROM `contents.size`/defaults, address and slot bounds, and per-domain
clock/reset/async metadata.  Until that holds, support stays refused:
`SourceSupported`, `OpSupported` and `memFree` are untouched.  PicoRV32 stays
**BLOCKED**.

---

## §63.31 — General source-layer preservation, on the ACTUAL `hwMemS` / `hwMemP`

Instruction 112's checkpoint. New module
`formal/lean/LeanSemanticPrimitives/Projection/HardwareMemSource.lean` (547
lines), built clean, 6 `#guard`s, **0 errors**, no `sorry`/admit/new axiom.
`hwMemInl` was added to `HardwareMem.lean` so the resolution equation can be
stated; nothing else in that file changed.

### Method, and a correction to §63.30's stated plan

§63.30 said the next step would be "object-level `memAt`/`romWords`/`srcValM`
preservation **via `evalFuel` on the resolved program** (the same explicit-lemma
method that worked for `getGo_body`)".  That is not what landed, and the change
is deliberate: the user's steer was to use the repository's production method,
`Surface.SEval` + `SEval_sound`, where it avoids de Bruijn and fuel plumbing.
It does — the fuel-counting route needs an exact bound per function and per call
depth, which `getGo_body`'s `4·len+8` shows is a whole proof each time — so the
per-function reasoning is over the NAMED program and crosses to `hwMemP` once.
`SEval` is defined without mentioning `resolve` or `Eval`, so the crossing is a
real check of binder order, not a restatement.

### What is proved

**Fresh resolution.**  `hwMemResolved_ok : hwMemResolved = .ok (hwMemP, hwMemInl)`
and `hwMemP_resolves : resolveProgram hwMemS = .ok (hwMemP, hwMemInl)`.  One
kernel reduction excludes the error branch; every id in `hwMemP` is assigned by
that call.

**Name/function correspondence, not preserved numbers.**  This was called out
explicitly: a preserved numeric id would not establish semantic preservation.
`sFn` routes through `idxOf` on the NAME list, and

```
sFn_of_prefix : (∃ ext, Q.funs = P.funs ++ ext) → sFn P f = some fd → sFn Q f = some fd
```

concludes with the SAME `SFun` record `fd`, from `idxOf_append` (a name found in
a prefix is found at the same position) and `List.getElem?_append_left` (that
position still indexes the same record).  It is stated over an ARBITRARY
extension, so it is the program shape that carries it, not the particular ten
functions.  `hwMemS_prefix` is `⟨_, rfl⟩`; `sFn_lift` is the instance.

**Transport of the whole old lemma set, recursive callees included.**
`SEval P` mentions `P` in exactly one place — the `call` rule's `sFn P f` — so

```
SEval_mono : (∀ f fd, sFn P f = some fd → sFn Q f = some fd) → SEval P σ e v → SEval Q σ e v
```

(mutual with `SEvalList_mono`, structural on the derivation) replays any
`hwS` derivation in `hwMemS`.  `lift` is the instance.  This is what carries
`srcVal` **and its callees `nthD` and `nz`** across, rather than re-proving them
or asserting that the ids still line up.

**`memAt` preserves the memory list** — `memAt_sound`, quantified over EVERY
encoded image list and EVERY `Int` index:

```
SEval hwMemS σ (.call "memAt" [eml, ei])
  (encMemImg (if 0 ≤ x then ms[x.toNat]?.getD absentImg else absentImg))
```

the exact selected image when present, the exact absent image (`[]`,
zero-width zero — matching `sourceValue`'s `fun _ => mk_bv 0 0`, via the
already-proved `absentImg_denote`) when absent.  **Negative indices are in
scope**: the object walks to `nil`, which is the absent image.

**`romWords` preserves ROM contents** — `romWords_sound`, quantified over every
content list: one word per entry, in order, each `mk_bv dw c`.  With
`romWords_ofMemConst` and `romWords_length` this pins the table at
`contents.size`, **not** `2 ^ aw`.

**`srcValM` on all six source forms** — `srcValM_agree`:

```
∃ v, SEval hwMemS σ (.call "srcValM" [esd, einp, efq, ems]) v
   ∧ SrcRel v (sourceValue i (StateM.denote st) sd)
```

with `SrcRel v (.bv b) := v = encBV b` and
`SrcRel v (.mem f) := ∃ m, v = encMemImg m ∧ MemImg.denote m = f` — the typed
slot relation at the source boundary.  Premises are the ORIGINAL bounds alone
(`SourceInputBound`, `SourceFlopBound`).  **`SourceSupported` is deliberately
not a premise**: answering the two memory forms is the point.  The memory cases
go through `srcMemImg_denote` and `ofMemConst_denote`; the four scalar cases,
asynchronous reset with its polarity included, are the original `srcVal` lemmas
lifted.  In `srcValM_memImg` the input and flop arguments are quantified over
ARBITRARY values — which is what proves the memory form does not consult them.

**Crossed to the resolved program, unconditionally.**  `srcValM_agree_resolved`
leaves no `resolve` hypothesis to the caller: `resolve_call_lits` pins the
resolved term for a literal argument list, and `srcValM_id : idOf "srcValM" =
some 43` is checked by reduction — the SAME id `HardwareMemChecks` executes.

### Non-vacuity, at the real shape

`srcValM_agree_lits` discharges every hypothesis with `SEval.lit`, so none is a
hidden unsatisfiable side condition.  Two `example`s then instantiate the
theorem at a Pico-shaped state (3-word 32-bit ROM, 32-word 32-bit image, 32-bit
flop, 1-bit reset) — one on `.memImg 1 5 32`, which `SourceSupported` would
refuse, and one on `.flopQAsync 0 32 0 0 false`.  Five `#guard`s then execute
the same call through `hwMemP` and confirm the selected image, the ROM's
initialised words, the exact absent image, and BOTH reset polarities.

### Axioms, stated precisely

`propext`, `Quot.sound` and (via `simp`/`Classical` in the crossing)
`Classical.choice` — Lean's own.  `srcValM_id` depends on none.  What is checked
for and absent: any **new unsafe** axiom — no `sorryAx`, no `Lean.ofReduceBool`,
no `Lean.trustCompiler` (verified over all 57 audit lines, 0 matches).

### Still owed, in order

`applyOpM` preservation through primitive → encoded host update →
`denote_writeBE`, discharging `InRange` and the width/address/byte-mask
conditions; the typed slot relation carried through `mkSourcesM`/`evalNodesM`;
`mkOutputs`/`flopNexts`/`memNext(s)` on active **and** quiet domains; generic
`mainM` adequacy against `interpretDesign`.  **The slot/value kind policy is
still prose** and the admission policy for mutable image shapes, ROM
`contents.size`/defaults, address and slot bounds and per-domain
clock/reset/async metadata is still undefined.  Until it holds, support stays
refused: `SourceSupported`, `OpSupported` and `memFree` are untouched, the
public route still refuses memory-bearing designs, and PicoRV32 stays
**BLOCKED**.  Matrix unchanged at **12 YES / 11 NO / 37 BLOCKED**.

---

## §63.32 — Operator preservation: `applyOpM` against `eval_op_cert`

New module `formal/lean/LeanSemanticPrimitives/Projection/HardwareMemOp.lean`
(311 lines), built clean, 2 `#guard`s, **0 errors**, no `sorry`/admit/new axiom.
Three disjoint obligations, kept apart because they rest on different premises.

**Delegation.**  `OpSupported_code_ne` decides, over the operator type, that no
supported operator has opcode 26 or 28, so both guards in `applyOpM` take their
false branch and `applyOp_agree` is replayed in the extended program by `lift`.
The scalar work behind it — SHL/SRA/Sext, the mux family, the comparison widths
— is **not re-proved and not re-stated**; it is the same code running.
`applyOpM_scalar_cert` then restates the result against the reference's single
`eval_op_cert` via `eval_op_cert_bv`.

**`Op_MemRead` (26).**  `applyOpM_memRead` composes three slot reads with
`MemImg.evalPrim_memRead`.  The result is a BIT VECTOR, which is what
`DirectCheck.opKind` says: only the write operators produce an image.

**`Op_MemWriteBE` (28).**  `applyOpM_memWriteBE` composes four slot reads and
the certificate payload `byte_w` with `MemImg.evalPrim_memWriteBE`;
`applyOpM_memWriteBE_denote` supplies the reference equality through
`denote_writeBE`.  `applyOpM_memWriteBE_disabled` covers the disabled write
**without** a range premise and shows the reference memory is unchanged too.

### The premises, stated rather than discharged

* **`InRange m (bv_uint a)` is a real premise of the ENABLED write.**  Out of
  range the finite table is deliberately not reference-equal: `cert_mem_write_be`
  updates the function at any address, while the primitive REFUSES
  (`valMemWriteBE_refuses_outOfRange`, executed in `HardwareMemChecks`).  A
  refusal is sound — it cannot answer wrongly — but it **is not agreement**, so
  the admission policy still owes this bound.  Nothing here weakens that.
* **Operand kinds are premises, in the shape `DirectCheck.depIsMem` already
  fixes**: position 0 of a memory operator is an image, every other position a
  bit vector.  They are stated as equations on the slot list, not assumed
  through a coercion, so a memory-valued operand in a scalar position cannot be
  silently read as a `BV`.  `DirectCheck` does already carry `sourceKind`,
  `opKind`, `slotKind`, `slotIsMem` and `depIsMem` — so the kind discipline is
  **not** purely prose on the certificate side; what is still missing is the
  check that binds it to the interpreter's slot list, which is the next step.
* `OpSupported`, `SourceSupported` and `memFree` are untouched; no design is
  admitted.

### Execution

`HardwareMemChecks` already runs both memory opcodes through `hwMemP` — enabled
and disabled reads, `byte_w = 1` (PicoRV32's bit-enable shape) and `byte_w = 8`
(byte lanes) writes read back through the same dispatch, the enabled
out-of-range refusal, and the malformed image.  What it did not run is OPERATOR
delegation, so two guards here pin that: `Op_And` (code 7) and `Op_SRA` (code
20, the operator whose correctness repair must not be lost) both reach the
original `applyOp` through `applyOpM` and answer `eval_op`.

### Axioms

`propext`, `Quot.sound` — Lean's own.  No **new unsafe** axiom: no `sorryAx`,
no `Lean.ofReduceBool`, no `Lean.trustCompiler`.

### Next

The typed slot relation carried through `mkSourcesM`/`evalNodesM` (which is
where `SlotValsM`/`DirectCheck.slotKind` must be tied together and the kind
policy stops being prose); `mkOutputs`/`flopNexts`/`memNext(s)` on active **and**
quiet domains; generic `mainM` adequacy against `interpretDesign`.  PicoRV32
stays **BLOCKED**; matrix unchanged at **12 YES / 11 NO / 37 BLOCKED**.

---

## §63.33 — The mixed scalar/memory slot gap, closed

Instruction 113's first required item.  New module
`formal/lean/LeanSemanticPrimitives/Projection/HardwareMemSlots.lean` (1084
lines), built clean, 2 `#guard`s, **0 errors**, no `sorry`/admit/new axiom.

### The gap, as the audit stated it — and it is real

`Hw.applyOp_agree` and all 27 per-operator lemmas behind it take the **global**
premise `SlotVals vals arg`, i.e. `∀ k < vals.length, vals[k] = encBV (arg k)`.
A memory-bearing design's slot environment contains `encMemImg` values, and
`encMemImg m = encBV b` is false for every `m` and `b`.  So the premise is
**unsatisfiable** in the environment the memory route actually runs in, and
§63.32's `applyOpM_scalar[_cert]` could not be instantiated there.  Runtime
behaviour was fine; the theorem was not usable.  That is now a theorem in the
module rather than a remark: `mixed_not_SlotVals` **proves** `SlotVals` false
for a three-slot list whose slot 0 is a Pico-shaped `encMemImg`.

### How it is closed, without weakening anything

`SlotVals` is **unchanged**, and so is every theorem stated with it —
`Hw.SlotVals`, `Hw.slot_read` and the whole `Hw.*_agree` family keep their exact
statements and remain what the memory-free route uses.  What is added is the
local condition on the dependencies actually read:

```
SlotHere vals arg d  :=  ∃ h : d < vals.length, vals[d]'h = encBV (arg d)
```

`SlotHere` carries the bound **and** the scalar reading together, so it replaces
each existing `d < vals.length` premise **one for one**: same name, same
position, same weakening step.  No premise was dropped — the old bound is
`SlotHere.bound`.  `SlotVals_SlotHere` proves the global premise gives the local
one at every dependency, and `applyOp_agree_of_On` derives `applyOp_agree`'s
**exact statement** from the mirrored version, so the mirror subsumes the
original and no meaning is lost.

**No scalar view of the environment is constructed and no proof-only
substitution is made**, so there is nothing to bridge observationally: the
mirrored lemmas run in the unmodified interpreter environment `objEnv vals`,
images and all.  That is possible because `Hw.slot_agree` is **kind-agnostic** —
it answers `vals[d]` whatever that slot holds — so the scalar premise was only
ever used to convert the read slot's value, which is exactly what `SlotHere`
provides.  **No memory image is dropped or replaced** anywhere.

### Coverage

All 27 operator lemmas mirrored, hence all seventeen `OpSupported` operators:
the folds (`Op_And`/`Op_Or`/`Op_Xor`/`Op_SHL`), the reductions
(`Op_Ror`/`Op_EQ`), `Op_Sum`'s payload split, `Op_MuxN`'s direct pick, the
fixed-arity `Op_Not`/`Op_SRA`/`Op_GetMask`/`Op_MuxBool`/`Op_Sext` and the four
comparisons.  SHL, SRA and Sext are covered with the rest.  The BV-level
correctness is **reused, not restated**: each mirrored proof is the original
proof with the slot read localized, so `evalOp_Ror`, `muxAt`, `cmpBool`,
`cmpLt_agree`, `nz_agree`, `slot_agree` and the `OperatorBridge` equations are
the same facts in the same places.

### The bridge, and non-vacuity on a genuinely mixed environment

`applyOpM_scalar_mixed` is the replacement for `applyOpM_scalar`: premises are
`OpSupported`, the arity, and `SlotHere` at each dependency, with `vals`
otherwise unrestricted.  `applyOpM_scalar_mixed_cert` restates it against
`eval_op_cert` with `SrcRel` carrying the kind.  Two `example`s then instantiate
it at a list holding a 32-word 32-bit `encMemImg` at slot 0 — `Op_And` and
`Op_SRA` over the two scalar slots, with the image **still present** — and two
`#guard`s confirm `hwMemP` answers the same on the same environment.

### Axioms

`propext`, `Quot.sound` — Lean's own; `SlotVals_SlotHere` depends on none.  No
**new unsafe** axiom.

### Still owed on this chain

The `DirectCheck.sourceKind`/`opKind`/`slotKind`/`slotIsMem`/`depIsMem` tie-in
and the general `mkSourcesM`/`evalNodeM`/`evalNodesM` invariant (source order,
reversed object environment, bounds, arity, topological prefix) — next.  Then
the enabled-write range obligation against real admitted shapes (including Pico
`aw=5`/`dw=32` and `byte_w=1`), outputs/flops/per-memory active-and-quiet
commits, and generic `mainM` adequacy through `StateRelR`/`ResultRelR`.
PicoRV32 stays **BLOCKED**; matrix unchanged at **12 YES / 11 NO / 37 BLOCKED**.

---

## §63.34 — Build-status correction, then source/node preservation and real write shapes

### Correction first: two of my build claims were not evidence

**The background notification was not a successful build.**  Task `b0d087ay1`
was my own `kill -KILL`: the lake producer exited **143**, and the `[exited with
code 0]` was the SHELL's exit, because my trailing `grep`/`head` masked the
producer.  I treated the notification as a compile; it was not one.  The same
hazard recurred at the end of the verification chain, where a zero-match
`grep -c` made the shell exit 1 on a run whose producer exited 0.

Fixed, not noted: every compile below goes through a helper that runs the
producer with its output REDIRECTED (never piped), captures `$?` immediately and
appends `PRODUCER_EXIT=<rc>` to the log.  The producer exit is read from that
line.  A pipe success, an empty error grep, a missing process in a broad
`pgrep` or an observation timeout is not a compile.

**My "lake is starved, not deadlocked" attribution was unsupported.**  Load 47
with 14 users does not establish CPU starvation and does not exclude I/O.  I
reproduced the real condition on my own producer (pid 2714602): main thread
`futex_wait_queue`, a worker in **D** state at `rpc_wait_bit_killable`,
`io_sq_thread` present, **0.72 s** CPU consumed and **151 KB** read after
minutes — an NFS RPC wait during import loading, not computation.  The oleans
themselves read fine (`dd` of `DirectCheck.olean`, `DesignSemantics.olean`,
`HardwareMemSlots.olean` all OK), so it is not file unavailability.  I retained
the single owned producer rather than relaunching, and it completed.

Two of my own polling helpers were self-matching (their `pgrep -f` patterns
appear in their own argv).  Both were identified by exact pid AND start_ticks
(2606370/218897387, 2611443/218904485), confirmed to be the `until pgrep` loops,
and stopped by pid.  No broad `pkill`, no lock deletion.

### Verified compiles (producer exits, this session)

| module | lines | `PRODUCER_EXIT` | errors | `#guard`s |
|---|---|---|---|---|
| `HardwareMemSlots` | 1086 | **0** | 0 | 2 |
| `HardwareMemShapes` | 195 | **0** | 0 | — |
| `HardwareMemTraversal` | 241 | **0** | 0 | — |
| `HardwareMemNodes` | 243 | **0** | 0 | 5 |

Unsafe-axiom scan across all four logs: **0** matches for `sorryAx`,
`Lean.ofReduceBool`, `Lean.trustCompiler`.  `propext`, `Quot.sound` and
`Classical.choice` are Lean's own.

### `HardwareMemTraversal` — the typed slot and general source traversal

`SlotV` makes the slot choice explicit — a bit vector OR a representable memory
image, never a coercion — with `SlotV.enc` (what the object holds) and
`SlotV.denote` (what the reference holds) kept apart.  `SrcRel_enc` proves they
agree for every slot, so `SlotV` IS §63.31's `SrcRel` in list form.

* `srcSlotV_denote` — the typed source value denotes `Compiler.sourceValue` on
  all six forms, with **no** supportedness premise.
* `srcSlotV_isMem` — its kind is `Direct.sourceKind`'s, on all six forms, and
  `srcSlots_slotKind` lifts that to `Direct.slotIsMem` at a source index.
* `mkSourcesM_agree` — general source traversal: arbitrary source list,
  arbitrary `StateM`, arbitrary input, **source order preserved**, accumulator
  reversal as `List.reverseAux` (the object's own recursion, not a separate
  `reverse` step), under the ORIGINAL input/flop bounds alone.
  `mkSourcesM_objEnv` lands it as `objEnv` of the typed slot list.

### `HardwareMemNodes` — node preservation and checker-derived kinds

`evalNodeM_scalar` / `_memRead` / `_memWriteBE` are the three node cases.  The
part that matters is where the operand kinds come from: **`Direct.DesignSemWF`
already carries `nodeKinds`**, so the kind at every dependency position is the
CHECKER's.  What is added connects it to the interpreter:
`depIsMem_false_of_supported` (decided over the operator type: a supported
operator has no memory operand at any position — which is exactly `SlotHere`'s
content), `depIsMem_memRead/_memWriteBE` (position 0 is the image, the rest are
bit vectors), `opKind_isMem_*` and `nodeSlot_slotIsMem` (the node's RESULT kind
at the slot it occupies, read off `slotKind`'s own definition).

**`Op_MemWrite` (27) is refused**, not given a defined-and-unproved answer: it
has no primitive and no `applyOpM` case.  `NodeAdmitted_ne_memWrite` proves an
admitted node is never it, and `nodeAdmittedB` is the same policy as a runnable
check — `#guard`s confirm it refuses `Op_MemWrite` and `Op_Sub` and admits
`Op_MemRead`, `Op_MemWriteBE 1` and `Op_SRA`.

### `HardwareMemShapes` — the enabled write's range, discharged from shapes

`FullTable aw m := m.words.length = 2 ^ aw` is the admitted MUTABLE shape.
`InRange_of_FullTable` discharges the write premise for **every** `aw`-bit
address with no condition on its value, because `bv_uint` is a remainder modulo
`2 ^ aw`.  So `InRange` now **follows from admission** instead of being carried
as an unexplained hypothesis (`writeInRange_of_FullTable`).

* **A ROM is not a mutable table.**  `ofMemConst_FullTable_iff` proves
  `ofMemConst dw contents` is a `FullTable aw` exactly when
  `contents.size = 2 ^ aw`; `romNotFull_example` is the real Pico-shaped
  counterexample (a 3-word 32-bit ROM at `aw = 5`); and
  `rom_write_outOfRange_refused` shows the primitive REFUSES the write rather
  than extending the table.
* **The shape survives.**  `setAt` is a `List.set`, so `writeBE_length`,
  `writeBE_dflt` and `writeBE_FullTable` keep the word count, the default and
  the admission across a write; `writeBE_other` shows no other address drifts.
* **A disabled write is an exact no-op** — `writeBE_disabled_eq` gives the
  image back unchanged, with no range premise.
* **Pico.**  `picoImg_FullTable` (`aw = 5`, `dw = 32`, 32 words),
  `pico_InRange` for every 5-bit address, `pico_InRange_after` across a write,
  and `pico_byte_w_one` recording that `Op_MemWriteBE 1` is BIT-enable
  semantics against the reference's own `cert_masked_update` (mask bit `i`
  selects data bit `i`, since `i / 1 = i`).  Two executed `example`s confirm
  `byte_w = 1` with mask 8 stores exactly 8, and `byte_w = 8` with mask 1 stores
  lane 0.

### Still owed on this chain

The `evalNodesM` fold itself — topological prefix, reversed environment,
dependency bounds and arity carried through, with the per-node kind
re-established from `nodeSlot_slotIsMem` — and a non-vacuous mixed case with a
real memory read, a real write and an intervening scalar node; then resolved
`Eval` statements through the production transfer; then
outputs/flops/active-and-quiet per-memory clock commits and generic `mainM`
adequacy against `StateRelR`/`ResultRelR`.  Public support stays refused:
`SourceSupported`, `OpSupported` and `memFree` are untouched.  PicoRV32 stays
**BLOCKED**; matrix unchanged at **12 YES / 11 NO / 37 BLOCKED** (FP 2 YES /
10 BLOCKED).

---

## §63.35 — The node fold, checker-derived kinds, and a checker-accepted mixed design

New module `formal/lean/LeanSemanticPrimitives/Projection/HardwareMemFold.lean`
(651 lines), plus `evalNodeM_memWriteBE_disabled` added to `HardwareMemNodes`.
Both verified with the producer's own exit: `PRODUCER_EXIT=0`, 0 errors,
7 + 5 `#guard`s, 0 matches for `sorryAx`/`Lean.ofReduceBool`/
`Lean.trustCompiler`.

### The invariant, and where each half comes from

```
slotValM D i st k := evalGraphG D.toGraphCert.topo D.toGraphCert (srcEnv …) k
```

is the reference slot value **with memories** — a `CertVal`, not projected
through `asBV`, so a memory slot keeps its function.  `Hw.slotVal` is this
projected, which is only correct where there are no memories.

```
PrefixRelM D i st m ss  :=  ss.length = m
                          ∧ ∀ k, (ss[k]).denote = slotValM D i st k
                          ∧ ∀ k, Direct.slotIsMem D k = some (ss[k]).isMem
```

* **Values**: `prefixRelM_sources` establishes it for the source prefix from
  `slotValM_source`; `nodeValM_rec` re-establishes it per node from
  `evalGraphG`'s own fixpoint property (`Hw.evalGraphG_rec`), at `CertVal`
  level — `Hw.nodeVal_rec` is the same fact with `asBV` applied and
  `OpSupported` assumed, so the projection step is **dropped rather than
  widened**.
* **Kinds**: `dep_kind_of_semwf` is literally `Direct.DesignSemWF.nodeKinds`,
  and `opArity_of_semwf` is literally `nodeShapes`.  Nothing about kinds or
  arities is a new hypothesis: both are what `checkDesign` already establishes.
  `dep_scalar` and `dep_mem` read a dependency out of the invariant at the kind
  the checker assigned it.

### The three node steps, and the fold

`node_step_scalar` / `_memRead` / `_memWriteBE`, dispatched by `node_step` on
`NodeAdmitted`.  Each is stated so the slot VALUE is produced independently of
the evaluation context — the `SEval` fact is a universally quantified conjunct —
which is what lets the fold pick the witness before fixing an environment.

`evalNodesM_agree` walks the dense node order (already topological, since
`topo` is `slotsFrom`), extends the witness list with `prefixRelM_snoc`, and
conses onto the object's newest-first environment via `objEnv_snoc` — the same
reversal the memory-free route uses.  `sourcesThenNodes` composes it with
`mkSourcesM`'s output with no gap, and `sourcesThenNodes_resolved` crosses to
`hwMemP` through the production bridge with **no `resolve` hypothesis left to
the caller** (`evalNodesM_id = some 47`, checked by reduction — the id the
executed checks call).

The `Op_MemWriteBE` step splits: the ENABLED branch consumes `NodeWriteRange`,
which is named rather than assumed away and is discharged by
`writeInRange_of_FullTable` from the admitted table shape; the DISABLED branch
needs no range premise and returns the image exactly, matching
`cert_mem_write_be`'s own else-branch.

### Non-vacuity — accepted by the REAL checker

`mixedDesign` has five sources (a mutable `memImg 0 5 32`, an address, a mask,
an enable and a scalar operand) and **three nodes in dependency order: a memory
`Op_MemRead`, an intervening scalar `Op_And` on its result, and an
`Op_MemWriteBE 1` whose data operand is that scalar node.**  `#guard`s confirm
`Direct.acceptsDesign mixedDesign`, that every node passes `nodeAdmittedB`, and
that `Direct.slotIsMem` reports image at slots 0 and 7 and bit vector at slots
1, 5 and 6 — i.e. the read's result is scalar and the write's is an image.
`mixedDesign_semwf` obtains `DesignSemWF` from `Direct.checkDesign_sound`, so
every kind premise above is instantiated from the checker's own output at a
shape that genuinely contains memories.

### Still owed

`mkOutputs`, `flopNexts` and `memNext(s)` on active **and** quiet domains, then
generic `mainM` adequacy against `StateRelR`/`ResultRelR`.  Public support stays
refused — `SourceSupported`, `OpSupported` and `memFree` are untouched and no
design is admitted by the public route.  PicoRV32 stays **BLOCKED**; matrix
unchanged at **12 YES / 11 NO / 37 BLOCKED** (FP 2 YES / 10 BLOCKED).

---

## §63.36 — The write-range premise was vacuous; geometry now rides the actual witness

### The defect, as a theorem

`NodeWriteRange` quantified over **every** `MemImg` with a given DENOTATION.
Denotation does not determine table length, so that premise was strictly
stronger than anything the interpreter's own image supplies — and it was FALSE
at the very design the module instantiated.  `MemImg.emptyZero` (no words,
32-bit zero default) denotes **exactly** `zeroImg 5 32` and holds no address at
all.  `evalNodesM_agree` and `sourcesThenNodes[_resolved]` assumed it for every
node, so the fold was **vacuous** on `mixedDesign`.  Checker acceptance,
`nodeAdmittedB` and the kind guards did not instantiate it, and I had presented
them as if they did.

That is no longer prose: `MemImg.denote_does_not_determine_geometry` proves

```
∃ m₁ m₂, denote m₁ = denote m₂ ∧ FullTable 5 m₂ ∧ ¬ FullTable 5 m₁
         ∧ ∀ x, ¬ InRange m₁ x
```

A second defect went with it: the range premise was imposed on reads, scalars
and DISABLED writes, none of whose proofs need it.

### The fix — geometry on the ACTUAL witness

`PrefixRelM` gains a slot assignment `G : Nat → Option Nat` and a fourth field

```
geom : ∀ k h g, (ss[k]'h) = .img g → ∀ aw, G k = some aw → MemImg.FullTable aw g
```

`G k = some aw` means slot `k` holds a MUTABLE table covering every `aw`-bit
address; `G k = none` means no such claim — a scalar slot, or a **ROM**, whose
table is its actual `contents.size` and which therefore cannot be written at
all, because the write step needs `G d0 = some aw` to reach `FullTable`.

* **Initialised at the sources.**  `srcGeomOf` reads the geometry off the
  source descriptors; `StateShapeOK` is an explicit premise that the incoming
  state's images have the geometry the certificate declares; and
  `memDesc_of_sourceOK` extracts from `checkSource` — the real checker — that a
  `memImg` source's `aw` **is** the declared memory's.  `srcGeom_of_StateShape`
  joins them.
* **Preserved at each node.**  Scalars and reads produce `.bv`, so the field is
  vacuous; the write produces `.img` and `MemImg.writeBE_FullTable` carries the
  shape across it.  `prefixRelM_snoc` re-establishes all four fields.
* **Split by need.**  `WritePropagates` (every write: the output slot inherits
  dep 0's geometry) is static.  `WriteAddrFits` — the only part mentioning an
  address — is **gated on `bv_nonzero` of the byte-enable**, so reads, scalars
  and disabled writes neither consume nor must satisfy it.  The disabled branch
  is proved without any range condition and returns the image exactly.
* `MemImg.InRange_of_FullTable_le` supplies the range from `a.width ≤ aw`, the
  form a certificate driving a narrow address into a wider table needs.

### Instantiated, not merely stated

`mixed_fold_resolved` discharges **every** premise on the checker-accepted
`mixedDesign` and produces the resolved `Eval` on `hwMemP`:
`designCertWF_of_semwf` from the checker, `StateShapeOK` from the state,
`mixedG_src` against the sources, `mixedG_prop` across the write, and
`mixedG_fits` from `mixed_addr_width` (the address is `input 0` at width 5, for
every input and state).  `mixed_fold_resolved_zero` is the full Pico-shaped
all-zero state — the one the broken premise could not be satisfied at — and
`mixed_fold_resolved_init` the initialised one.

Twelve `#guard`s execute it.  On an initialised table (word `k` holds `k+1`):
address 20 reads 21, the intervening scalar `Op_And` with `0xF` gives 5, and
the ENABLED byte-enabled write stores 5 — so **the word at 20 changes from 21
to 5**, address 19 still holds 20, and the table is still 32 words.  With a
zero byte-enable the image comes back **exactly** equal to the original.

### Headers corrected

The fold's header now records the defect and the split premise.  The nodes
module no longer implies its `WriteInRange` discharges a traversal's
obligation: it is a standalone one-image/one-address shape lemma, and the
fold's premise is `WriteAddrFits`.

### Axioms

`propext`, `Quot.sound`, `Classical.choice` — Lean's own.  No **new unsafe**
axiom in any module.

### Still owed

`mkOutputs`, `flopNexts` and `memNext(s)` on active **and** quiet domains, then
generic `mainM` adequacy against `StateRelR`/`ResultRelR`.  Public support stays
refused: `SourceSupported`, `OpSupported` and `memFree` are untouched and
`Op_MemWrite`/`Op_Sub` remain refused.  PicoRV32 stays **BLOCKED**; matrix
unchanged at **12 YES / 11 NO / 37 BLOCKED**.

---

## §63.37 — Clock-controlled memory commit, general and instantiated

New module `formal/lean/LeanSemanticPrimitives/Projection/HardwareMemCommit.lean`
(641 lines), `PRODUCER_EXIT=0`, 0 errors, **14 `#guard`s**, 0 matches for
`sorryAx`/`Lean.ofReduceBool`/`Lean.trustCompiler`.

### The general lemmas

* `memNext_agree` — on an **active** declared clock the memory's own
  `MemoryDesc.nextImg` slot is selected out of the typed final `PrefixRelM` as
  the ACTUAL image that slot holds (`slotImg`, `memSlot_img`); on a **quiet**
  clock the EXACT prior first-order image comes back through `memAt` on the
  incoming state's image list.  `firesAt_agree` supplies the edge test, and the
  clock ordinal bound, the slot bound and the slot KIND are taken from the real
  records — `Direct.DesignSemWF.memKinds`, `memClocks`,
  `DesignCertWF.slotsInRange` — never assumed.
* `memNexts_agree` — the whole array, for every suffix and every starting
  index, matching the object's own index increment (`memNextsOf`).
* `memNextOf_denote` / `memNextsOf_denote` — correspondence with
  `Compiler.srcMemNext`, stated as an equality of `MemImg.denote`, i.e. of
  `Int → BV` **functions**.  That is every address at once; **no read is
  sampled anywhere in the statement**.
* `memNextOf_FullTable` / `stepState_shape` — the committed array again
  satisfies `StateShapeOK`, from the invariant's geometry on the active branch
  and from the incoming state's shape on the quiet one, so the design can take
  another step.

The old denotation-universal claim is not reintroduced anywhere: every
obligation is stated over the actual witness image, as §63.36 established.

### `NextImgGeomOK`, and how a checker is meant to establish it

`NextImgGeomOK D G` says each memory's `nextImg` slot carries that memory's
declared address width.  `checkDesign` already establishes the KIND half
(`memKinds`).  The geometry half is the natural refinement — propagate a slot's
address width the way `WritePropagates` does at a write node and compare with
`MemoryDesc.aw`, a static walk over the dense slot order.  It is carried as a
**premise**; the checker extension is **not** claimed to exist.

### What `StateShapeOK` is not

It constrains **table length only**.  The module header names what remains
explicit and owed: **word and default widths** (`dw` bits), **data and mask
widths** at a write beyond the address width `WriteAddrFits` already pins, and
**ROM initialisation** — a `memConst` keeps its actual `contents.size` and its
own default, is assigned no mutable geometry, and is therefore never a write
target.  `FullTable` is not complete admission and is not described as such.

### Instantiated on two memories with DISTINCT clocks

`twoMemDesign`: six sources (two mutable images, address, byte-enable mask,
read enable, scalar), four nodes (read → intervening scalar `Op_And` → one
byte-enabled write per memory), two memories on clock ordinals **0** and **1**.
`Direct.acceptsDesign` accepts it; `twoMem_commit` discharges every premise and
delivers the fold, the commit and the preserved shape together.

Executed controls, with memory 0 holding 21 at address 20 and memory 1 holding
120:

| edges | memory 0 | memory 1 |
|---|---|---|
| `#[true, false]` | commits → 5 | **exactly** `imgB` |
| `#[false, true]` | **exactly** `imgA` | commits → 5 |
| `#[false, false]` | **exactly** `imgA` | **exactly** `imgB` |
| `#[true, true]` | commits → 5 | commits → 5 |
| `#[true, true]`, zero byte-enable | **exactly** `imgA` | **exactly** `imgB` |

The both-quiet row is a genuine control, not a tautology: a `#guard` records
that the held values (21, 120) **differ** from the committed value (5), so a
proof that ignored the clock would be wrong there.  A further guard confirms
both tables are still 32 words after the commit.

### Still owed

Mixed-environment `mkOutputs` and `flopNexts` — where local scalar dependencies
must suffice and the old global `SlotVals` cannot be assumed — then generic
`mainM` adequacy through `StateRelR`/`ResultRelR`, composing the ACTUAL
`mkSourcesM` evaluation with the node fold rather than starting from
host-created source slots.  Public support stays refused:
`SourceSupported`, `OpSupported` and `memFree` are untouched and
`Op_MemWrite`/`Op_Sub` remain refused.  PicoRV32 stays **BLOCKED**; matrix
unchanged at **12 YES / 11 NO / 37 BLOCKED**.

---

## §63.38 — Mixed-environment outputs and flops, and generic `mainM` adequacy

Two new modules, both `PRODUCER_EXIT=0`, 0 errors, no unsafe axioms:
`HardwareMemOutFlop.lean` (228 lines) and `HardwareMemMain.lean` (244 lines).

### Outputs and flops, localized

`Hw.mkOutputs_agree` and the flop family take the GLOBAL `SlotVals`, which
§63.33 proved unsatisfiable wherever an `encMemImg` is present.  All five —
`mkOutputs_agreeOn`, `rstActive_agreeOn`, `capture_agreeOn`, `flopNext_agreeOn`,
`flopNexts_agreeOn` — are mirrored with the local `SlotHere`, exactly as the 27
operator lemmas were.  `SlotVals` is unchanged and every original theorem keeps
its meaning; each mirrored proof is the original with the slot read localized,
so **no scalar behaviour is restated**: active/quiet clocks, the flop ENABLE,
SYNCHRONOUS reset, ASYNCHRONOUS reset with its polarity and the old-state hold
are the same code running.

### The premises are the checker's

`outputs_scalar` and `flops_scalar` derive `SlotHere` at exactly the slots these
lemmas read, from `Direct.DesignSemWF.outputKinds` and `flopKinds` together with
`DesignCertWF.slotsInRange` — not from assumptions.

### The array bridge

`memNextsOf_denote_array` lifts the pointwise `srcMemNext` lemmas to the
COMPLETE `RuntimeState` memory array: same length, same `Int → BV` function at
every index.  That is the length/index bridge the pointwise lemmas needed.

### `mainM_adequate`

```
∃ r, SEval hwMemS [] (.call "mainM" [encDesign D, encEdges e, encInput i, encStateM st]) (encResultM r)
   ∧ r.denote = Compiler.interpretDesign D e i (StateM.denote st)
```

`mkSourcesM`, the corrected node fold, `mkOutputs`, `flopNexts` and `memNexts`
are composed into one cycle.  **The node fold's evaluation witness is retained
and used** — the environment `mainM` hands to the output, flop and memory stages
is the one `evalNodesM` actually produced, not one assumed to exist.  §63.37's
`twoMem_commit` discarded that witness; this does not.

`mainM_resultRel` restates it through `StateM.ResultRelR`, i.e. the object's
value decodes to a representable witness whose denotation **is** the reference
`RuntimeResult`.

No host interpreter is called as the implementation, and nothing stops at a
helper-only fixture: the statement is about `hwMemS`'s own `mainM`.

### Still owed

The whole-`mainM` fixture instantiation — decoding the returned state and
feeding it to a SECOND step, with a negative control that detects a
quiet-domain commit or lost reset.  `FullTable`/`StateShapeOK` still describe
length only; word/default/data/mask/address/ROM and next-image geometry
checking, its soundness and reachable-state preservation all remain before
public support can broaden.  `SourceSupported`, `OpSupported` and `memFree` are
untouched; `Op_MemWrite`/`Op_Sub` still refused.  PicoRV32 stays **BLOCKED**;
matrix unchanged at **12 YES / 11 NO / 37 BLOCKED**.

---

## §63.39 — The resolved whole-cycle entry, instantiated and executed

`HardwareMemMain.lean` extended and a new `HardwareMemFixture.lean` (354 lines),
both `PRODUCER_EXIT=0`, 0 errors, **19 `#guard`s**, no unsafe axioms.

### The resolved entry — verified, not relabelled

`hwMemS_entry : hwMemS.entry = "mainM"` and `mainM_id : idOf "mainM" = some 50`
are checked by reduction, and `main_ne_mainM` records that this is a DIFFERENT
function from the memory-free `main`.  `mainM_adequate_resolved` crosses to
`hwMemP` through the production transfer `Surface.SEval_entry`, which resolves
the entry and all four literal arguments itself — no `resolve` hypothesis is
left to the caller.

`mainM_adequate` also now concludes
`NextImgGeomOK D G → StateShapeOK D r.nextState`: the RETURNED state carries the
declared geometry, which is what lets a second step run.

### The fixture, with premises DISCHARGED

`fullDesign` — accepted by `Direct.checkDesign` — has **two clock domains**, a
**mutable** `aw = 5`/`dw = 32` table and a **read-only `memConst` ROM** whose
table is its actual `contents.size = 3`, a flop with an **enable** and a
**synchronous** reset on clock 0, a second flop with an **asynchronous** reset
on clock 1, and a `byte_w = 1` write — PicoRV32's bit-enable shape.

Every antecedent of `mainM_adequate_resolved` is proved for it: `DesignSemWF`
from `checkDesign_sound`, node admission, arity, `fullG_src` against the
sources, `fullG_prop` across the write, `fullG_fits` from `full_addr_width`, the
state's shape, the input/flop bounds, both clock bounds and the flop count.
**Computational agreement is not instantiation**, and this is the instantiation.
The ROM gets geometry `none`, so a write to it could not be discharged.

`fullTwoSteps` chains two steps: the second runs on the state the FIRST
returned, and both agree with `Compiler.interpretDesign` on their own inputs and
edges.  `flops_size_of_denote` reads the returned flop count off the adequacy
equation rather than assuming it.

### Executed controls

At address 1 the table holds 2 and the ROM holds 8, so `Op_Xor` gives 10.

| case | observed |
|---|---|
| clock A only | output 10, word 1 → 10, flop 0 captures 10, flop 1 holds |
| both quiet | flops and the image preserved **exactly** |
| **negative control** | the quiet state **differs** from the active one |
| async reset, both clocks quiet | flop 1 → 255; flop 0's SYNC reset does **not** fire |
| **negative control** | with the reset low, flop 1 is **not** 255 |
| sync reset, clock A firing | flop 0 loads 0 rather than din 10 |
| zero byte-enable | image **exactly** preserved despite clock A firing |
| **two steps** | decoded state fed back: reads 10, xors with the ROM's 8, writes 2 |

The ROM's actual contents (8 at address 1) and its DEFAULT above them (0 at
address 20) are both checked.

### Still owed — the next checkpoint

The checked ADMISSION POLICY: `FullTable`/`StateShapeOK` remain **length only**.
Word/default/data/mask/address widths, mutable-versus-ROM images, source and
slot bounds, clock/reset/async metadata and static next-image geometry all need
a checker, its soundness, and initialization/preservation lemmas, with runtime
address-fit DERIVED from checked shape and typing.  Until then
`SourceSupported`, `OpSupported` and `memFree` stay untouched and
`Op_MemWrite`/`Op_Sub` stay refused.  PicoRV32 remains **BLOCKED**; matrix
unchanged at **12 YES / 11 NO / 37 BLOCKED**.

---

## §63.40 — The width discipline, so `WriteAddrFits` can be DERIVED

New `formal/lean/LeanSemanticPrimitives/Projection/HardwareMemWidth.lean`
(115 lines), `PRODUCER_EXIT=0`, 0 errors, no unsafe axioms.

### Why this first

Instruction 119 requires the checked admission policy to **derive** the enabled
write's address fit from checked shape and typing, not to take `WriteAddrFits`
as the checker's own premise.  Deriving it needs a fact the repository did not
have: that a slot's runtime value has the width the certificate declares.  That
is what this module proves, and it is the foundation the static checker is
built on — not the checker itself.

### What is proved

* `eval_op_width : (eval_op op w l).width = w` — for **every** operator, by case
  analysis over `LGraphOp`.  The supporting helper widths (`mk_bv`, `bv_resize`,
  `bv_bitwise`, `bv_not`, `bv_sdiv`, `bv_sra`, `bv_get_mask`, `bv_set_mask`) and
  the two folds (`Op_And`/`Op_Or`/`Op_Xor` and `Op_SHL`) are proved alongside,
  the folds by induction on the operand list.
* `cert_mem_read_width` — the memory READ returns the node width on **both**
  branches, enabled and disabled.
* `eval_op_cert_width_of_supported` and `eval_op_cert_width_memRead` — the same
  at `CertVal` level, which is the form the node fold produces.
* `sourceValue_width_input/_const/_flopQ/_flopQAsync` — every SCALAR source
  returns its declared width, the asynchronous-reset source on both its reset
  and its hold branch.

### Scope, stated exactly

This is the width half only.  **The executable static admission checker, its
soundness theorem, the runtime state/input/edge policy, the
initialization/preservation lemmas, the admission-to-resolved-main composition
and the rejection controls are NOT in this commit.**  `StateShapeOK` still
proves table length only; `SourceSupported`, `OpSupported` and `memFree` are
untouched; `Op_MemWrite` and `Op_Sub` stay refused.  PicoRV32 remains
**BLOCKED**; matrix unchanged at **12 YES / 11 NO / 37 BLOCKED**.

---

## §63.41 — The executable static admission checker, and what it does not yet prove

Header correction, import re-layering, and a new
`formal/lean/LeanSemanticPrimitives/Projection/HardwareMemAdmit.lean`
(377 lines, **24 `#guard`s**), `PRODUCER_EXIT=0`, 0 errors, no unsafe axioms.

### Two corrections first

`HardwareMemWidth`'s header said the width facts were "lifted to `slotValM` by
induction over the dense order".  **They were not**, and the claim is withdrawn
in the source.  The module also imported `HardwareMemFixture`, i.e. upward; it
now imports `HardwareMemMain`, so the width and admission layers sit BELOW the
fixtures and a fixture can import and instantiate the checker without a cycle.

### The checker

A slot-type inference over the dense source/node order:
`SlotTy := bv w | tbl aw dw | rom dw n`.  `tbl` and `rom` are deliberately
distinct — a `memConst` keeps its real `contents.size` and its own default and
is typed `rom`, so it can be READ but never written.

* `Op_MemRead [m,a,en]` — `m` an image of either kind, `a`/`en` scalars, and
  against a `tbl aw _` the address no wider than `aw`.
* `Op_MemWriteBE bw [m,a,d,be]` — `m` must be `tbl aw dw`; address no wider than
  `aw`; node width and data width exactly `dw`; `0 < bw`; and the mask must
  cover every data bit, `dw ≤ wb * bw`.  At `bw = 1` that is Pico's bit-enable
  rule (32-bit mask for 32-bit data at `aw = 5`); at `bw = 8` it is the lane
  rule (4 mask bits suffice).  The result propagates `tbl aw dw`.
* Anything else — `OpSupported` with all-scalar dependencies.  `Op_MemWrite` and
  `Op_Sub` have no rule and are REFUSED.
* Every `MemoryDesc.nextImg` is checked against that memory's `aw` and `dw`.

`Direct.checkDesign` is **retained**, not replaced: `admitsB` conjoins it.

### Soundness — proved

`admits_semwf`, `admits_wf`, `admits_geom_src` (the source-consistent geometry
assignment, via the `infer_foldl_prefix` lemma that the source prefix survives
the whole walk) and `admits_nextImgGeom`.  That is three of
`mainM_adequate_resolved`'s antecedents plus the WF facts.

### Executed controls

`mixedDesign`, `twoMemDesign` and a new `byte_w = 8` `laneDesign` all pass.
Refused, each a small changed-source variant: a ROM write target; a `nextImg`
geometry mismatch in `aw`; one in `dw`; an address wider than the table; a data
width that is not `dw`; a node width that is not `dw`; plain `Op_MemWrite`;
`Op_Sub`; a memory operand in a scalar position; an out-of-range `nextImg`; an
undeclared clock ordinal (caught by the retained `checkDesign`); and the lane
design with `byte_w = 1`, where a 4-bit mask cannot cover 32 data bits.

### NOT proved here — stated plainly

* **`NodeAdmitted`/arity and `WritePropagates` from the checker** — these need
  the node-type recurrence and are not claimed.
* **The `slotValM` width/kind induction and the DERIVED `WriteAddrFits`** — the
  per-operator and per-source width facts exist (§63.40) but are not yet
  carried to the value the interpreter computes at a slot.
* **Applying soundness to a concrete design.**  `admitsB D = true` is currently
  carried by `#guard` (compiler evaluation) for the concrete designs: neither
  `decide` nor `rfl` reduces `admitsB` to a literal, because the fold over the
  node list does not evaluate in the kernel here.  That is executed evidence,
  **not** a theorem, it is said so in the module, and no theorem depends on it.
* The runtime state/input/edge policy, its codec correspondence and
  preservation, and the admission-to-resolved-main composition.

`FullTable`/`StateShapeOK` still prove table length only.  `SourceSupported`,
`OpSupported` and `memFree` are untouched.  PicoRV32 stays **BLOCKED**; matrix
unchanged at **12 YES / 11 NO / 37 BLOCKED**.

---

## §63.42 — Kernel acceptance integrated, and the checker-to-static-premises bridge

`HardwareMemAdmit.lean` and `HardwareMemFixture.lean` extended; both
`PRODUCER_EXIT=0`, 0 errors, no unsafe axioms.

### Two corrections

* **`admitsB` DOES reduce in the kernel.**  §63.41 said "the fold over the node
  list does not evaluate in the kernel".  That was wrong: `decide +kernel`
  evaluates it.  What had failed was default `decide`/`rfl`, which is a
  different thing and does not license the stronger claim.  The module text is
  corrected and the acceptance lemmas are now THEOREMS:
  `mixedDesign_admits`, `twoMemDesign_admits`, `laneDesign_admits`,
  `fullDesign_admits`, each `propext` only.
* **The `#guard` count in §63.41 was 24; the executable count is 22.**  Two of
  the 24 were prose mentions, not commands.  Corrected.

### The per-node recurrence

`nodeTy` now consults its lookup ONLY through `c.deps.toList.map ty`, which
makes `nodeTy_congr` immediate.  With `inferStep_size`, `infer_foldl_size`,
`infer_foldl_prefix` and `infer_foldl_none`, `infer_foldl_node` turns the fold
into a per-node equation, and `admits_node_ty` specialises it to the real
design using `DepsBounded` for the topological bound.

### `StaticPremises`, from one Boolean check

`admits_static_premises : admitsB D = true → StaticPremises D` delivers **all
seven** static antecedents of `mainM_adequate_resolved`:

| premise | source |
|---|---|
| `DesignCertWF` / `DesignSemWF` | retained `Direct.checkDesign` |
| source-consistent `geomAt` | `infer_foldl_prefix` |
| `NodeAdmitted` | `nodeTy_admits` — `nodeTy` refuses whatever `nodeAdmittedB` refuses, so `Op_MemWrite` and `Op_Sub` cannot type |
| `ArityOK` | `arityOK_of_opArity`, bridging `Direct.opArity` to `Hw.RequiredArity` |
| `WritePropagates` | `nodeTy_write_geom` — a typing write consumed a MUTABLE image and returns its geometry |
| `NextImgGeomOK` | the `nextImg` check |

Instantiated as theorems on all four designs: `mixedDesign_static`,
`twoMemDesign_static`, `laneDesign_static`, `fullDesign_static`.

### Still open

**`WriteAddrFits` is NOT derived yet** — it is a runtime condition and needs the
`slotValM` kind/width induction over the dense order, using §63.40's operator
and source width lemmas.  That is the next increment, together with the runtime
state/input/edge policy, its codec correspondence and preservation, and the
admission-to-resolved-main composition.  `FullTable`/`StateShapeOK` still prove
length only.  `SourceSupported`, `OpSupported` and `memFree` untouched.
PicoRV32 **BLOCKED**; matrix **12 YES / 11 NO / 37 BLOCKED**.

## §63.43 — scalar slot widths, and `WriteAddrFits` DERIVED from the check

`LeanSemanticPrimitives/Projection/HardwareMemSlotTy.lean` (new, builds clean;
`fullDesign`/`HardwareMemFixture` and `HardwareMemWidth` rebuilt on top of it).

### The circularity that had to be avoided

`sourcesThenNodes` and `evalNodesM_agree` **assume** `WriteAddrFits`, so neither
can be used to prove it: an induction over the dense order that invoked the fold
lemmas would be arguing in a circle.  It turned out no such induction is needed.
`eval_op_cert_width_nonMem` says every admitted non-write operator returns the
NODE WIDTH **at any argument list whatsoever** — it never inspects the
arguments — and `eval_op_cert_memRead_width` says the same for `Op_MemRead`.
The dense-order case analysis therefore collapses to a source/node split with no
recursive hypothesis:

| slot | inferred type | value width |
|---|---|---|
| source | `srcTy sd` | `srcTy_bv_width` — all four scalar source families, async reset on **both** branches |
| node | `nodeTy ty c` | `nodeTy_bv_width` forces `w = c.width`; the operator lemmas give the width unconditionally |

* `admits_slot_width : admitsB D = true → k < D.numSlots → tyAt D k = some (.bv w)
  → (slotValM D i st k).asBV.width = w`.

### The derived premise

* `admits_writeAddrFits : admitsB D = true →
   ∀ c ∈ D.nodes.toList, WriteAddrFits D i st (geomAt D) c`.

`nodeTy_write_geom` hands back the inferred `tbl aw dw` of the written image
together with `ty addr = some (.bv wa)` and `wa ≤ aw`; `admits_slot_width` turns
that inferred `wa` into the runtime width of the actual address value.  The
address fit is **read off the checker's own inference**, not assumed.  It is
enabled-only, exactly as `WriteAddrFits` is stated.

### Composition

* `admits_mainM_resolved` — `admitsB D = true` plus the runtime premises
  (`SourceInputBound`, `SourceFlopBound`, flop/memory clock bounds, flop-array
  size, `StateShapeOK`) gives the **actual resolved `mainM`** evaluation, with
  `r.denote = Compiler.interpretDesign D e i (StateM.denote st)` and
  `StateShapeOK D r.nextState`, at `G = geomAt D`.  Every static antecedent
  comes from `admits_static_premises` (§63.42); the write-fit from the theorem
  above.

### The hand-written geometries are gone from the proof path

`geomAt` agrees with each hand assignment **everywhere**, not just at the
guarded slots — by `funext` with a finite kernel split under `numSlots` and
`geomAt_out` above it:

| control | theorem |
|---|---|
| `geomAt fullDesign = fullG` | `geomAt_full` |
| `geomAt mixedDesign = mixedG` | `geomAt_mixed` |
| `geomAt twoMemDesign = twoMemG` | `geomAt_twoMem` |

So the three hand `WriteAddrFits` case analyses are re-proved from the generic
theorem alone — `full_fits_checked`, `mixed_fits_checked`,
`twoMem_fits_checked` — and the instantiated fold is re-proved from
`admits_static_premises` alone: `mixed_fold_checked` and `twoMem_fold_checked`
use **no** `*G_src`, `*_admitted`, `*_arity`, `*G_prop` or `*G_fits`.
`fullStep` now takes the checker route outright (`admits_mainM_resolved
fullDesign_admits`), so the full fixture carries no hand write-fit at all;
`fullG_fits` and the other `fullG` lemmas are retained below it purely as
independent controls, and the decoded two-step changing-memory and quiet/reset
`#guard`s are unchanged.

`HardwareMemFold` and `HardwareMemCommit` sit BELOW the checker, so their own
`mixedG_fits` / `twoMemG_fits` cannot call it; those hand proofs stay where they
are, superseded by the checked versions proved above them.

Axioms: `propext`, `Classical.choice`, `Quot.sound` only — no `sorryAx`
anywhere in the chain.

### Still open

This is the **conditional** admission wrapper.  `FullTable` and `StateShapeOK`
still prove LENGTH only: the complete executable runtime state/input/edge policy
— real image presence, table/word/default widths, flop and input bounds, edge
discipline, memory initialisation, exact active/quiet/disabled behaviour and the
full clock/reset/async metadata through the real codecs, plus initialisation and
returned-state preservation and the codec/`RuntimeState` correspondence — is the
next increment.  `SourceSupported`, `OpSupported` and `memFree` untouched; the
public route still refuses memory-bearing designs.  PicoRV32 **BLOCKED**;
matrix **12 YES / 11 NO / 37 BLOCKED**.

## §63.44 — the executable runtime state / input / edge policy

`LeanSemanticPrimitives/Projection/HardwareMemRuntime.lean` (new, builds clean,
22 executed guards); `HardwareMemFixture` re-pointed onto it.

`admitsB` decides the DESIGN.  Six premises of `admits_mainM_resolved` were
still runtime facts carried by hand at every call site.  They are now one
Boolean.

### The shape is no longer length-only

`StateShapeOK` says `words.length = 2 ^ aw` and nothing else — which is all the
address fit needs, so it was all that was proved.  It is not what a real state
must satisfy.  `MemShapeOK md m` adds the two missing facts:

* every word is `md.dw` wide, and
* so is `m.dflt` — the word EVERY out-of-range read returns.

`MemShapeOK.toFullTable` discharges the length statement every existing proof
consumes, so nothing below had to change.  The executed control is explicit:
`narrowImg` has 32 words of width **7** against a `dw = 32` memory —
`#guard MemImg.FullTable 5 narrowImg` passes, `memImgOKB … narrowImg == false`.

### The four checks

| check | premise discharged |
|---|---|
| `stateOKB D st` | `StateShapeOK D st` (via `stateOKB_shapeW`, which gives the stronger `MemShapeOK`) and `D.flops.size ≤ st.flops.size` |
| `inputOKB D i.size` | `SourceInputBound` for every source — `input idx`, and `flopQAsync`'s reset index, which reads the INPUT vector |
| `flopBoundOKB D st.flops.size` | `SourceFlopBound` for every source |
| `edgesOKB D e` | `f.clock < e.size` and `md.clock < e.size`, so `firesAt` never reads past the edge vector |

`runtimeOKB` is their conjunction, and

* `admits_run : admitsB D = true → runtimeOKB D i st e = true → ∃ r, Eval … ∧
  r.denote = Compiler.interpretDesign D e i (StateM.denote st) ∧
  StateShapeOK D r.nextState`

takes **two Booleans and nothing else**.  `fullRun` is the full fixture's
instantiation; `fullStep`/`fullTwoSteps` keep the hypothesis form unchanged.

### Refusals that bite

Executed, all `== false`: wrong word width, wrong default width, a table half
the declared length, a declared image ABSENT from the state, a two-memory design
given one image, `input 3` against a 3-element input vector, clock 1 against a
one-element edge vector, an empty edge vector, a flop vector shorter than the
design's flop array, and the two `runtimeOKB` compositions of those.

### Still open

**Returned-state preservation at the WORD level.**  `admits_run` concludes the
length-level `StateShapeOK` for `r.nextState`, exactly as
`admits_mainM_resolved` does — not `stateOKB D r.nextState`.  Unlike §63.43's
scalar width lift, that one IS a recurrence: a `tbl` slot produced by a write
has the shape of the image it wrote into, so the dense order has to be walked.
It is the next increment, together with the memory-initialisation statement and
the codec/`RuntimeState` correspondence for the strengthened shape.
`SourceSupported`, `OpSupported` and `memFree` untouched; the public route still
refuses memory-bearing designs.  PicoRV32 **BLOCKED**; matrix
**12 YES / 11 NO / 37 BLOCKED**.

## §63.45 — word-level returned-state preservation

`HardwareMemRuntime` §§6–7 (same module, extended); `HardwareMemFixture` gains
`fullRunTwo`.

### Why the shape is recoverable without touching `PrefixRelM`

The object side of a memory is a `MemImg` — a word list and a default.  The
reference side is a FUNCTION `Int → BV`, which has neither, and §63.42's
`denote_does_not_determine_geometry` says the LENGTH genuinely is lost in
between.  The WIDTHS are not: a full table's every address is in range, so each
word is the denotation at its own index, and the default is the denotation at
any negative address.  Hence

* `memShape_denote_width` — shape gives pointwise width, and
* `memShape_of_denote` — `FullTable` plus pointwise width gives shape back.

That is the whole reason this increment needed **no** change to `PrefixRelM`,
`sourcesThenNodes`, `mainM_adequate` or any of their call sites: the missing
facts are recovered from the reference function, which the adequacy equation
already pins.

### The induction

`admits_slot_memwidth`: under `admitsB` and the word-level state check, a slot
inferred `tbl aw dw` or `rom dw n` has a reference value returning `dw`-wide
words at EVERY address.  Unlike §63.43's scalar lift this IS a recurrence — a
written table's values are the values of the image it wrote into, everywhere the
write did not land — so it is a strong induction on the slot index, with the
memory operand reached through `DepsBounded`.  It is still not circular: every
statement is about `slotValM`, the reference side, so none of the fold lemmas
that assume `WriteAddrFits` is involved.

Supporting checker facts, both read off `nodeTy`/`admitsB` rather than assumed:

* `nodeTy_tbl_op` — only a byte-enabled write can type as a TABLE; every other
  arm of `nodeTy` returns `.bv` or nothing;
* `nodeTy_write_dw` — the write's NODE WIDTH equals the table's data width
  (`nodeTy_write_geom` gives the address fit, this gives the word width);
* `admits_nextImgTy` — the checker pins the next-image slot's FULL type, both
  widths, to the memory it commits;
* `memDesc_dw_of_sourceOK` — `checkSource` pins BOTH of a `memImg` source's
  declared widths, not just the address width.

### Preservation, and the loop

* `admits_run_pres` — `admitsB` and `runtimeOKB` give the resolved cycle AND
  `stateOKB D r.nextState = true`: the written branch from the induction, the
  quiet branch from the incoming state.
* `flopBoundOKB_of_flopSize` — the flop-vector bound is not independent;
  `checkSource` already pins every `flopQ`/`flopQAsync` source to a declared
  flop.
* `admits_run_two` — two steps, the second on the state the first returned.
  Only the second step's INPUT and EDGE vectors need checking; its state does
  not, because the policy is preserved.  `fullRunTwo` instantiates it.

So `runtimeOKB` is now a loop invariant, not a one-off precondition, and
`FullTable`/`StateShapeOK` are no longer the only thing proved about a state.

Axioms: `propext`, `Classical.choice`, `Quot.sound` — no `sorryAx`.

### Still open

Memory INITIALISATION (building a legal `StateM` for a design, rather than
checking one) and the codec/`RuntimeState` correspondence for the strengthened
shape are the next increment, then the public support/DCERT2 generation and
readiness drivers.  `SourceSupported`, `OpSupported` and `memFree` untouched;
the public route still refuses memory-bearing designs.  PicoRV32 **BLOCKED**;
matrix **12 YES / 11 NO / 37 BLOCKED**.

## §63.46 — initialisation, and the codec correspondence

`HardwareMemRuntime` §8; `HardwareMemFixture` gains `fullRunInit`.

Checking a state is half a policy.  If no state passed, `admits_run` would be
vacuous — so the design itself must be able to produce one.

* `initStateOf D` — each flop at its DECLARED reset value, each memory a zero
  table of its declared geometry, built from the design alone.
* `initStateOf_ok : stateOKB D (initStateOf D) = true` — via `zeroImg_shape`,
  so the canonical state passes the word-level check, not merely the length one.
* `admits_run_init` — **every admitted design can actually be run.**  The state
  premise is discharged by construction and the flop bound by
  `flopBoundOKB_of_flopSize`, so only the input and the edge vector remain.
  `fullRunInit` instantiates it with no state hypothesis at all.

### `RuntimeWF`, on the nose

`stateOKB` deliberately accepts a LONGER vector than the design declares,
because the interpreter reads by index.  `RuntimeWF` wants the sizes exactly,
and the canonical state has them: `initStateOf_runtimeWF`, with
`runtimeWF_of_sizes` for any other state whose vectors are the declared sizes.
The two are kept separate on purpose — neither is weakened to fit the other.

### The codec

`StateM.decStateM_encStateM` is the round trip and was already proved.  What the
policy needs is that the state coming BACK out of the encoder is the one that
went in, so it passes the SAME check and denotes the same `RuntimeState`:
`stateOKB_codec`, `initStateOf_codec`, and `StateRelR_initStateOf` for the
relation the interpreter's correctness statements use.  The shape rides through
the literal argument; it is not re-established on the far side.

Eleven further executed guards (7 in `HardwareMemRuntime`, 4 in the fixture):
the canonical state of `mixedDesign`, `twoMemDesign` and `fullDesign` passes,
with the right memory count, flop count and 32-word tables, and `runtimeOKB`
accepts it against a real input and edge vector.

### Still open

Public support / DCERT2 generation and the readiness drivers; BTA with the
design static and exactly edges/input/state dynamic; annotation erasure and
proved specialisation; production arity-3 residual checking at its returned
bound; whole PicoRV32 execution with genuine memory transitions and phase
metrics.  `SourceSupported`, `OpSupported` and `memFree` untouched; the public
route still refuses memory-bearing designs.  PicoRV32 **BLOCKED**; matrix
**12 YES / 11 NO / 37 BLOCKED**.

## §63.47 — the declared flop width, and the strengthened route

`HardwareMemRuntime` §9; `HardwareMemFixture` §6.  Reported gap: `stateOKB`
checks the flop VECTOR's size but no declared flop WIDTHS, so `runtimeOKB`
accepts `fullDesign` with two 7-bit values against 32-bit descriptors even with
both clocks quiet.  Confirmed, closed, and the confirmation is executed here:

| control | result |
|---|---|
| `runtimeOKB fullDesign inRun fullNarrowFlops eOff` | `true` — the reported gap |
| decoded `runMain fullNarrowFlops inRun eOff` | returns flops of width **7** — the gap is observable, not hypothetical |
| `flopShapeOKB` / `stateFullOKB` / `runtimeFullOKB` on the same state | `false` |

It was a gap in what the policy SAYS, never a contradiction of the adequacy
equation.  `stateOKB`, `runtimeOKB` and every theorem about them are unchanged;
`stateFullOKB`/`runtimeFullOKB` are their conjunction with `flopShapeOKB`.

### Why the raw INPUT widths stay unchecked — stated, not assumed away

Every source read RESIZES to the width its descriptor declares: `sourceValue`
of `.input idx w` and of `.flopQ idx w` is `bv_resize w …`, both `.flopQAsync`
branches resize, and `srcFlopNext`'s CAPTURE branch is
`bv_resize f.width (rho f.din).asBV`.

**Corrected in §63.50.**  This section originally said a raw vector's widths
"cannot be observed".  That is wrong: resizing fixes the RESULT width, but
`bv_resize w x = mk_bv w (bv_uint x)` and `bv_uint x = x.value % 2 ^ x.width`
reads the SOURCE width, so a differently-wide raw entry can change the VALUE.
Width noninterference is neither proved nor claimed.  What is true, and what
`inputOKB` rests on, is that raw input widths are UNNECESSARY here:
`admits_run_full` is an AGREEMENT statement and both sides are applied to the
same vector `i`, so whatever widths it carries they are read identically.
Asserting declared widths on the raw vector would be a hypothesis the agreement
never uses, and would reject inputs the reference accepts.

The STORED FLOP width is not like that, and that asymmetry is the whole point:
`srcFlopNext`'s HOLD branch is `s.flops[idx]?.getD (mk_bv f.width 0)`, which
copies the stored value into the next state VERBATIM, with no resize.

### The commit rule, branch by branch

`srcFlopNext_unfold` names the two guards (`resetAsserted`, `flopEnabled`);
each branch is then given its exact VALUE and its width:

| branch | condition | value | premise |
|---|---|---|---|
| `srcFlopNext_reset` | reset asserted and (edge **or** `asyncReset`) | `mk_bv f.width f.resetValue` | none |
| `srcFlopNext_capture` | no reset, edge, enable high | `bv_resize f.width (rho f.din).asBV` | none — `din`'s own width is irrelevant |
| `srcFlopNext_hold` | no reset, quiet clock **or** low enable | `s.flops[idx]?.getD (mk_bv f.width 0)` | the policy's, and only here |

### Preservation and composition

`StateM.denote` carries the flop array VERBATIM, so the returned widths come
straight off the adequacy equation — the same move §7 made for the memories.
`admits_run_full` concludes `stateFullOKB D r.nextState = true`;
`admits_run_full_init` runs from `initStateOf` with no state hypothesis at all;
`admits_run_full_two` composes two steps with only the second step's input and
edge vector left to check.  `fullRunFull`, `fullRunFullInit` and
`fullRunFullTwo` instantiate them.

### Controls retained and added

The decoded two-step changing-memory control (word 1: 2 → 10 → 2), the
disabled-write control, the quiet-clock controls and the reset/polarity
controls are all unchanged.  Added, decoded on `fullDesign`: the widths survive
`eOff`/`eA`/`eB`/`eAll`; the ASYNCHRONOUS reset acts on a quiet clock and still
at width 32 (flop 1 takes 255 with both edges low) while the SYNCHRONOUS one
holds on the same step; and the two-step now also checks
`runtimeFullOKB`/`stateFullOKB` on the intermediate and final states.
`laneDesign` (`byte_w = 8`) and the `byte_w = 1` refusal beside it are retained
and carried into the strengthened route by
`stateFullOKB laneDesign (initStateOf laneDesign)`.

The module header is reconciled: word-level preservation is no longer described
as future work, and the one remaining scope limit named there is the one §9
closes.

### Scope of `initStateOf`

It is a legal-state / non-vacuity witness — zero mutable tables and
reset-valued flops — and nothing more.  It does **not** establish an arbitrary
RTL startup contract, and the drivers must use the supplied dynamic state or a
justified initialisation adapter, preserving imported initialised contents,
actual-size ROM bounds and defaults, and reset/async/clock/component metadata,
refusing unsupported initialisation rather than silently zeroing it.

Axioms: `propext`, `Classical.choice`, `Quot.sound` — no `sorryAx`.

### Still open

Public support / DCERT2 generation and the readiness drivers under this
admission policy; BTA with the design static and exactly edges/input/state
dynamic; annotation erasure and proved specialisation; production arity-3
residual checking at its returned bound; whole PicoRV32 generation and
execution with genuine transitions and phase metrics.  `SourceSupported`,
`OpSupported` and `memFree` untouched; the public route still refuses
memory-bearing designs.  PicoRV32 **BLOCKED**; matrix
**12 YES / 11 NO / 37 BLOCKED**.

## §63.48 — the memory route's checked support answer, and its actual entry

`LeanSemanticPrimitives/Projection/MemSupport.lean` (new, builds clean, 12
executed guards).  Dependency chain built to reach it: `PartialEvaluatorCorrect`,
`ProjectionCorrect`, `ResidualFragment`, `ProjectedStep`, `CertLoad` — all
producer 0.

### The scalar route is untouched

`CertLoad.supportReport`, `SupportReport.allOK`, `runOne`, `SourceSupported`,
`OpSupported` and `SupportedByProjection` are unchanged.  `memFree` is still a
REFUSAL there, and `scalar_refuses_memory` proves it has to be: a design with a
memory makes that conjunct false, so the scalar verdict is `false` whatever else
holds.  Broadening those predicates would subtract meaning from the scalar
adequacy theorems, so the alternative is stated BESIDE them, not in them.

### The alternative answer

`MemSupport.Report` carries the scalar verdict and the conjunct that decides it
(`scalar`, `memFree`) together with the three components of the memory admission
check (`accepts`, `sized`, `nextImgs`, plus `typed` as a diagnostic), so a
refusal names the part that failed.

* `admitOK_eq : (report D).admitOK = admitsB D` — the report **is** the check,
  not a summary of it.

### The entry and codec correspondence

`mainM` is a function ADDED to `hwS`; the scalar `main` is still there, unedited,
under its own name.  `entry_name`, `entry_index` and `entry_distinct`
(`idOf "main" ≠ idOf "mainM"`) record that the memory route is a different entry
of the same program, not a replacement.

* `run_of_report` — a report-admitted design, run on a state/input/edge vector
  `runtimeFullOKB` accepts, evaluates at the ACTUAL resolved entry
  `hwMemP.entry` on the real `encDesign`/`encEdges`/`encInput`/`encStateM`
  literals to a value that DECODES through `StateM.decResultM`, and the decoded
  result denotes `Compiler.interpretDesign`'s — with the returned state passing
  the same strengthened policy.
* `run_of_report_init` — the same from the design's own canonical state.

### Readiness

`Report.line` and `verdict` are pure, so they are guarded; `sayReport` wraps
them in `CertLoad.say`'s shape so a driver can print them without any change to
`runOne`.  Executed:

| design | verdict |
|---|---|
| `fullDesign` | `SCALAR ROUTE: refused (memFree) -- MEMORY ROUTE: admitted` |
| `scalarDesign` (memory-free) | `SCALAR ROUTE: supported` — unchanged where it was already right |
| `mixedDesign` whose write target is a ROM | `REFUSED BY BOTH ROUTES` |

`mixedDesign`, `twoMemDesign` and `laneDesign` (`byte_w = 8`) all report
`admitOK`.

### Scope

No whole-unit admission credit follows from this.  A design passing `report` is
a design the memory route's own theorems apply to, nothing more; `initStateOf`
remains a legal-state witness and not an RTL startup contract, and a driver with
real initial contents must pass them in as the dynamic state.

Axioms: `propext`, `Classical.choice`, `Quot.sound` — no `sorryAx`.

### Still open

Wiring the readiness lines into a production DCERT2 driver that preserves
imported initialised contents, actual-size ROM bounds/defaults and
reset/async/clock/component metadata and refuses unsupported initialisation;
BTA with the design static and exactly edges/input/state dynamic; annotation
erasure and proved specialisation; production arity-3 residual checking at its
returned bound; whole PicoRV32 generation and execution with genuine transitions
and phase metrics.  PicoRV32 **BLOCKED**; matrix **12 YES / 11 NO / 37 BLOCKED**.

## §63.49 — BTA, proved specialization and the arity-3 residual, on the MEMORY entry

`LeanSemanticPrimitives/Projection/MemProjection.lean` (new, builds clean,
12 executed guards).

### Binding time: design static, exactly edges/input/state dynamic

```
hwMemA = bta hwMemP hwMemInl [.stat, .dyn, .dyn, .dyn] 200
```

the same division the scalar route uses, on the memory entry.  Proved, not
guarded:

* `hwMemAP_entry_params` / `hwMemAP_entry` — the entry's division really is
  `[.stat, .dyn, .dyn, .dyn]`: ONE static argument (the certificate) and three
  dynamic ones (edges, input, state), in that order;
* `hwMemAP_erases : eraseProgram hwMemAP = hwMemP` — **annotation erasure**, via
  `bta_erases`: the annotated program is the memory interpreter with annotations
  ON it, not a different program;
* `hwMemAP_wf` — via `bta_sound`.

Both kernel reductions need the raised heartbeat budget, as the scalar ones do.

### Proved specialization

`Hw.specialize_correct_of` bakes in `encState`/`ResultRel`, which are the
memory-FREE codecs, so it cannot be instantiated here.
`specializeFour_correct_of` is the same proof with the static value, the three
dynamic values and the answer left as `Val`s — the source/dynamic SPLIT is
preserved exactly, pinned by the same `[.stat, .dyn, .dyn, .dyn]`.  The original
is untouched.

`memEntry_unique` turns `admits_run_full`'s existence statement into the `iff`
the specializer consumes: `Eval_det` makes the value the memory entry produces
the ONLY one.

* **`projectMemDesign_correct` — the first Futamura projection on the memory
  entry.**  Specializing the memory interpreter to a design yields a residual
  that, run on the three dynamic arguments of one cycle, computes exactly what
  the shared reference semantics computes, as an `iff` over `Eval` with no fuel
  in the conclusion.  The specialization fuel enters only through
  `projectMemDesign D = .ok R`.

### Production arity-3 residual checking, at its returned bound

The residual passes the **same** production checker, `Hw.checkResidual`,
unchanged: one function, entry 0, **arity 3**, in the fragment, with the bound
it returns.  `MemSimulator`/`MemSimWF`/`mkMemSim` mirror the scalar bundle, with
`memFree` replaced by `admitsB`.

* `runMemSim_correct` — for an honest bundle on a `runtimeFullOKB`-accepted
  state, the run TERMINATES within `sim.bound` (`.outOfFuel` unreachable, via
  `checkResidual_complete`), decodes through `StateM.decResultM`, agrees with
  `Compiler.interpretDesign`, and leaves a state passing the same policy.

### Executed, with genuine memory transitions through the RESIDUAL

`mixedDesign` at `initState` (word `k` holds `k + 1`): address 20 holds 21,
ANDed with `0xF` is 5, and the enabled write stores 5.

| control | result |
|---|---|
| one cycle, clock firing, mask `0xFFFFFFFF` | word 20: **21 → 5** |
| same edge, mask 0 | image EXACTLY unchanged |
| quiet clock, write enabled | image EXACTLY unchanged |
| two cycles, second on the state the first returned | word 20 stays 5, and `stateFullOKB` holds on BOTH returned states |
| the residual itself | `funs.map arity == [3]` — the design is gone |

Axioms: `propext`, `Classical.choice`, `Quot.sound` — no `sorryAx`.

### Still open

Whole PicoRV32 generation and execution with phase metrics; wiring the
readiness lines into a production DCERT2 driver that preserves imported
initialised contents, actual-size ROM bounds/defaults and
reset/async/clock/component metadata and refuses unsupported initialisation.
PicoRV32 **BLOCKED**; matrix **12 YES / 11 NO / 37 BLOCKED**.  No whole-unit
admission credit follows from any fixture or proof compile here.

## §63.50 — two reporting corrections, the production memory-route driver, and Pico

### Correction 1: the scalar refusal reason was INFERRED

`MemSupport.verdict` printed `refused (memFree)` for every design the memory
route admitted.  The audit's control is decisive: a ROM-only design has **no
declared memories**, so `memFree` is TRUE and the scalar refusal is `sources`.
Fixed by reading the reason off the scalar report:

* `scalarFailures D` lists the fields that are actually `false`;
* `memFree_mem_scalarFailures` / `sources_mem_scalarFailures` — when a field is
  false, the list NAMES it;
* `scalarFailures_ne_nil` — a scalar refusal always names at least one.

Executed on the control: `memFree == true`, `scalarFailures` contains
`"sources"` and does NOT contain `"memFree"`, and the verdict is no longer the
string the audit caught.  On `fullDesign` the actual fields are
`memFree,sources,ops` — `ops` because `Op_MemRead` is not in `OpSupported`,
which the old wording never showed.

### Correction 2: "raw input widths cannot be observed" was FALSE

Withdrawn in `HardwareMemRuntime`'s §9 and in §63.47 above.  `bv_resize w x` is
`mk_bv w (bv_uint x)` and `bv_uint x` is `x.value % 2 ^ x.width`, so resizing
fixes the RESULT width while READING the source width: a differently-wide raw
entry can change the VALUE.  **Width noninterference is neither proved nor
claimed.**  The accurate statement, and the one `inputOKB` actually rests on, is
that raw input widths are UNNECESSARY here: `admits_run_full` is an AGREEMENT
statement and both sides are applied to the same vector, so whatever widths it
carries are read identically by both.

Neither correction touches a proof.

### The production driver

`formal/lean/scripts/dcert2_memroute.lean`, wired into
`pass/lean/scripts/fp_cov_dcert2.sh` as the `memroute` phase.

| phase | what runs | distinct exit |
|---|---|---|
| parse | `CertIO.admitAny`, under the format's own policy | 8 |
| support | scalar report VERBATIM + the fields that failed + the memory report | 6 (memory route refuses) |
| specialize | `mixDriver` on `hwMemAP` at the requested budget | 3 out of fuel / 4 failed |
| check | `Projection.Hw.checkResidual`, unchanged, at its RETURNED bound | 5 |
| execute | gated by `runtimeFullOKB`, decoded by `StateM.decResultM` | 1 / 7 policy refused |

* Metadata is RETAINED and reported, not dropped: clock names, per-flop width /
  din / enable / resetPin / resetValue / polarity / clock ordinal / async flag,
  per-memory aw/dw/nextImg/clock, and for every `memConst` the actual contents,
  the `contents.size` table bound (printed beside `2 ^ aw` so the difference is
  visible), the default's value AND width, and an out-of-contents read.
* The DECODED next state is what the following cycle runs on.
* The bundle is built only by `mkMemSimAt`, and `mkMemSimAt_design` (new) proves
  the result is bound to the design it was handed; `projectMemDesignAt` /
  `MemSimWFAt` generalise the fixed-fuel forms so the driver's budget is the one
  that was checked.  An arbitrary `MemSimulator` cannot be handed to it.
* The certificate's own bytes are digested (FNV-1a) and printed; the harness
  additionally binds cert/driver/lean/LEAN_PATH content hashes per attempt.
* `init=zero` is `initStateOf` and is printed as a **legal-state witness, not an
  RTL startup contract**; `init=ramp` is caller-SUPPLIED contents; **any other
  startup contract is refused, not replaced** (exit 2).
* Scalar behaviour is preserved: `memroute` is NOT in `--only all`, because a
  memory-bearing certificate currently stops the scalar row at `support`, and
  folding in a sixth phase would change that row's disposition.

### Exercised

Fixtures, `init=ramp`, 6 cycles, all agreeing with `Compiler.interpretDesign`
(outputs, flops, and memories compared EXHAUSTIVELY over the table, never
sampled):

| fixture | residual | bound | transitions |
|---|---|---|---|
| `full` (2 clocks, sync+async reset, ROM, `byte_w=1`) | 427 terms, arity [3] | 22 | 1 word, 3 distinct outputs, 2 distinct flop vectors |
| `mixed` (`byte_w=1`) | 181 terms | 15 | 1 word |
| `twomem` (two memories, distinct clocks) | 271 terms | 17 | 1 word in EACH |
| `lane` (`byte_w=8`) | 181 terms | 15 | 1 word |

Real retained DCERT2 certificates: `tiny_byte_ram` **admitted and executed** —
178 terms, bound 24, 2 words changed, reference agreement — and `two_domain`
(memory-free) likewise.  `tiny_ram`, `tiny_sync_ram`, `mem_domain` and
`mem_nondefault` are REFUSED with the exact reason now named:
`node N (Op_MemWrite, width 8, 4 operands) has no slot type` — the memory route
implements `Op_MemWriteBE`, not the non-byte-enabled write.

Negative controls, each with its own exit: ROM write target → 6; narrow flops →
**7, all four cycles `policy-refused`, none run on an unchecked state**;
unsupported startup contract → 2; fuel 5/5 → 3, reported as a failure AT THAT
BUDGET.

Through the harness: `tiny_byte_ram memroute exit=0` (producer status 0, wall
17.21 s, maxrss 6,621,352 KiB), `tiny_ram memroute exit=6`.  `--only support`
on the same certificate still reports exit 0, unchanged.

### Whole PicoRV32, at the pinned source and configuration

Fresh generation attempted, not reused.

* source `picorv32.v` sha256 `0836050971b3c6cdd28ac3b1e5719a67fb645161912bef1e472e63995ceb0622`, upstream rev `ef203c2`
* compiler `bazel-bin/lhd/lhd` sha256 `414d5dcbf99b37fea2ceac2b81c131401c9bef2a7e580613f5b388199af63f2a`
* configuration: `--reader yosys-slang --top picorv32 --set formal.lean.mode=verified_compiler --set yosys.setundef=zero -- --ignore-assertions`
* **producer exit 1**; wall 4.01 s, user 3.91 s, sys 0.21 s, peak RSS 324,480 KiB
* phases: `inou.yosys.tolg` 2094.8 ms … `pass.lean` **45.6 ms**

```
[ERROR] pass.lean: unsupported certificate op(s): latch (first at n_1460).
68 node(s) in this design have no certificate encoding.
```

The LGraph was produced; the `lean/` emit directory is **empty**.  So there is
no DCERT2 for this unit, and support, BTA, specialization, residual check and
execution are **not applicable** — not refused, not attempted.  This is a
SEMANTIC refusal, an unencodable component, **not** a fuel or resource cap: the
certificate encoder has no latch form, and 68 nodes need one.  Supplying one
means a new component in the SHARED `DesignCert`, its reference semantics, its
checker case and its interpreter case — it is not a driver change and nothing
here fakes it.  **PicoRV32 remains BLOCKED on `pass.lean: latch`.**

Matrix unchanged: **12 YES / 11 NO / 37 BLOCKED**.  No whole-unit admission
credit is taken from any fixture, certificate or compile above.

## §63.51 — B09 and B10: the blocker was a CAP, and it is cleared

Continuing the master FP units after the Pico increment.  Of the thirteen unit
directories under `cov12x5`, three had a recorded `verified_compiler` attempt
and all three failed:

| unit | recorded refusal | kind |
|---|---|---|
| B09 `cva6_issue_scoreboard` | `pass.lean: node n_684 (node) has width 3536 > max_width=1024` | **cap** |
| B10 `cva6_frontend` | `pass.lean: node n_10692 (node) has width 2080 > max_width=1024` | **cap** |
| B12 `picorv32` | `pass.lean: unsupported certificate op(s): latch` | **semantic** |

`max_width` is a documented, configurable label on `pass.lean`
(`pass/lean/pass_lean.cpp:31`, "Hard cap on node Bits width; 0 or 'unlimited' =
no cap (default 1024)").  A cap is not a semantic refusal, so B09 and B10 were
re-run from their own retained `s2_single_edge/lg` at the pinned top and mode
with `--set formal.lean.max_width=unlimited`:

| unit | producer exit | wall | user+sys | peak RSS | `pass.lean` | emitted |
|---|---|---|---|---|---|---|
| B09 `issue_stage_gate` | **0** | 4.90 s | 4.82 + 0.04 s | 203,140 KiB | 312.0 ms | `issue_stage_gate_Lgraph.lean` 1,252,028 B, sha256 `4afcfb50…` |
| B10 `frontend_gate` | **0** | 5.82 s | 5.71 + 0.07 s | 211,820 KiB | 248.7 ms | `frontend_gate_Lgraph.lean` 970,752 B, sha256 `cb73b273…` |

Both now emit the verified-compiler certificate module where they previously
refused.  Nothing was weakened to get there: `max_width` bounds the WIDTH a node
may carry in the emitted certificate, and raising it admits wider nodes rather
than relaxing any check.

**A caution recorded with it.**  The same parameter also bounds a memory's
ADDRESS width (`pass/lean/graph_access.cpp:313`).  Removing the cap outright
would admit an `aw` whose `2 ^ aw`-word table cannot exist, and the memory-route
driver builds such a table for `init=zero`/`init=ramp`.  These two units declare
no memories, so the run above is unaffected, but `max_width=unlimited` must not
be adopted as a blanket default for a memory-bearing design without an address
bound of its own.

### What is NOT claimed

Generation is the first gate, not the unit.  The emitted modules have **not**
been built: `d3_guarded_build.py` refuses in this session —

```
d3_guarded_build: REFUSING -- no delegated cgroup v2 base.
The build would run unguarded, which is what this wrapper exists to prevent.
```

— and the session's own cgroup (`/user.slice/user-1008.slice/session-*.scope`)
is root-owned with `cgroup.subtree_control` not writable, so there is no
delegation to use.  A bare `lean` on a real design is exactly what the guarded
runner exists to prevent, so the build gate was **not** run and no result is
claimed for it.  That is an environment limitation, not a refusal and not a
pass.

The ten remaining unit directories have no recorded `verified_compiler` attempt
at all, so they are untouched here.

Matrix unchanged: **12 YES / 11 NO / 37 BLOCKED**.  B09 and B10 advance from a
generation refusal to a generation PASS awaiting the guarded build; B12 stays
BLOCKED on `pass.lean: latch`.

## §63.52 — CORRECTIONS to §63.50 and §63.51

Both sections overstated what their evidence showed.  Reconciled against the
retained artifacts.

### §63.50's Pico conclusion was out of scope

The raw-Verilog recipe I ran really does exit 1 on 68 latches, and that
observation stands **for that recipe**.  What it does NOT establish is anything
about the whole unit, because it **omits the retained single-edge
normalization stage**.  The retained evidence says the opposite of what §63.50
implied:

| stage | exit |
|---|---|
| `B12_picorv32/s1_compile` | **0** |
| `B12_picorv32/s2_single_edge` | **0** |
| `B12_picorv32/vc_opt` | **0** |

and `vc_opt/lean/picorv32.dcert` exists: **70,067 bytes**, sha256
`f85a2a9db8ac89ba71adac23ccbefac9bf290301974733545c0f939d3abf49c9`, format v2.

So whole Pico **does** have a DCERT2 certificate, and the claim "PicoRV32
remains BLOCKED on `pass.lean: latch`" was wrong as a statement about the unit.
The correct statement: the **raw-Verilog route** refuses on latches; the
**normalized route** produces a certificate.  Both are kept; neither is erased,
and normalization is not irrelevant.

### §63.51's B09/B10 conclusion was wrong

B09 and B10 already have FP YES and verified-compiler YES, including generation
checks and eight-cycle readiness, and both already have a retained
`verified_compiler_mw` attempt at exit 0.  My "newly unblocked" modules are
**byte-identical** to those retained artifacts:

* B09 `4afcfb504459d9a787b5e2e5b730ea7d4f36f79680fab9d1326af21fb4fca461`
* B10 `cb73b273cb7aba6d15eaefaba6758171ec221f4a22ff24afd99eea2d9eca8d4e`

They were **not** newly unblocked and were **not** awaiting a first build.  What
§63.51 actually recorded was a duplicate of an already-passing gate.  The
`d3_guarded_build.py` refusal is retained as an honest observation about that
duplicate attempt, and it is **not** a task-wide blocker.

### Emitter provenance

The binary I used, `bazel-bin/lhd/lhd` sha256
`414d5dcbf99b37fea2ceac2b81c131401c9bef2a7e580613f5b388199af63f2a`, is dated
before the current source: the retained `verified_compiler_mw/lean/` carries a
`.dcert` sidecar and **my new directories do not**, although the current
`pass/lean/pass_lean.cpp` emits one in `verified_compiler` mode.  A path does
not establish a source revision.  Fresh emission must use a source-bound
current functional build; the retained certificates and this binary observation
are kept separately.

### Standing

The reporting fixes of §63.50 (actual scalar failure fields; the withdrawn
width claim), the configurable-fuel proof, and every audited memory semantic
are unaffected by these corrections.  Matrix stays **12 YES / 11 NO /
37 BLOCKED**.

## §63.53 — whole Pico on the memory route, and the driver's production contract

### Whole PicoRV32, on the NORMALIZED route

The retained `vc_opt/lean/picorv32.dcert` (70,067 bytes, sha256
`f85a2a9d…`, format v2) was run through the memory route end to end.

```
shape: sources 1302  nodes 2279  outputs 18  flops 174  memories 1  clocks 1
memory 0: aw 5 dw 32 nextImg slot 3488 clock 0 (table 32 words)
scalar support: wf true memFree false sources false ops false arities true
                flopClocks true | ALL false
scalar refused on: memFree,sources,ops
memory route: accepts true typed true sized true nextImgs true | ADMITTED true
```

**Whole Pico is ADMITTED by the checked memory route** — exactly the
aw5/dw32 memory, the `MemRead` nodes and the `MemWriteBE 1` the matrix records.
The run then specialized, passed the unchanged production residual checker,
and executed at its returned bound with the reference agreeing on every cycle:
`step outcomes ok,ok,ok,ok`, producer exit 0.

A resource note, kept separate from any refusal: the first attempt died in
`mixDriver` with `deep recursion ... increase --tstack`.  That is a STACK cap,
not a semantic refusal — the production harness already runs
`ulimit -s unlimited` and the standalone runner did not.  With the limit raised
the same input succeeds.

### The double specialization the timing hid

That run reported `specialize 676932 ms  check 659085 ms`.  The "check" was a
**second full specialization**: the driver called `projectMemDesignAt` and then
`mkMemSimAt`, which specializes again.  Fixed by a new constructor that binds
the residual already in hand:

* `MemProjection.mkMemSimOf D R` — admission plus the production residual check,
  and no specialization;
* `mkMemSimOf_parts` / `mkMemSimOf_wf` — the bundle is honest exactly when the
  caller can EXHIBIT the specialization that produced `R`, so this is not a way
  to launder an arbitrary `Program` into a checked one.

### The supplied-state contract

`init=ramp` was called "caller-supplied" in §63.50.  It is not: it is an
internally generated pattern.  Corrected — `zero`, `ramp` and `narrow` are now
labelled SYNTHETIC controls, and a real loader was added:

```
memstate v1
flops reset            # explicit: use the design's DECLARED reset values
flop <width> <value>
mem  <aw> <dw> <default>
word <addr> <value>
```

The supplied state is read, used AS IT IS, carried through the production codec
(`encStateM`/`decStateM` round-trip CHECKED, not assumed) and gated by
`stateFullOKB` before any cycle runs.  Nothing is defaulted and nothing is
manufactured; `flops reset` is the file's explicit request and the provenance
line says that part is synthetic.  Malformed input is refused by line:

```
REFUSING: …malformed_word_before_mem.memstate: line 2: `word` before any `mem`
```

Executed control (`scripts/memstate/full_nonzero_default.memstate`): a NONZERO
default of 255 and non-ramp words 11/22/33 survive the driver —
`initial memory 0 words [11,22,255,255,255,255]` — the codec round trip holds,
one word changes over six cycles, and the comparison is COMPLETE.

### Comparison is a STATUS, not a boolean

`agreement=true` was printed even when `aw > 12` skipped the memory check, and
`nocompare` still claimed agreement.  Both fixed.  The status is now
`COMPLETE` / `PARTIAL (what was skipped)` / `DISABLED` / `FAILED (what)`, only
`COMPLETE` earns credit, and the final verdict says which it was:

```
RESULT: MEMROUTE EXECUTED … -- reference comparison DISABLED by nocompare
        -- NOT full credit; nothing was compared …
```

`COMPLETE` now means the memory **array length**, **every** admitted table word,
the **default** (read at `-1`) and the **out-of-range** read above the table, as
well as outputs and flops.  `CmpStatus.meet` keeps the weakest status, so one
skipped cycle cannot be reported as a complete comparison.

The **returned-state policy is a gate**, not an informational line: a run whose
returned state fails `stateFullOKB` exits 7.

### `--selftest`: 22 controls, all PASS

Including the ones this instruction asked for: a **default-only** mismatch is
rejected (every word equal, only `dflt` differs), a length mismatch is rejected,
an over-large table is reported SKIPPED rather than passed, a memory-array
length mismatch FAILS rather than skips, `DISABLED`/`PARTIAL` do not earn
credit, and the state parser refuses a missing header, a bad keyword, a
non-numeric field, a `word` before any `mem` and an out-of-table address.

Matrix stays **12 YES / 11 NO / 37 BLOCKED**.

## §63.54 — the ten blocked FP units, by their ACTUAL refusal

Instruction 125 directs attention to the ten blocked units.  Surveyed from the
retained attempt records, not re-run: each one's first failing stage, its exit,
the class the tool assigned it, and its message.

| unit | first failing stage | exit | class | refusal |
|---|---|---|---|---|
| B01 `coreet_dcache` | `s1_compile` | 1 | internal | `Concat lane 0 (of 2, MSB-first) driver 'mux_94508' occupies 2 bits but its declared window is 1` |
| B02 `cva6_hpdcache` | `s1_compile` | 6 | syntax | combinational loop in `cva6_hpdcache_subsystem_gate` |
| B03 `coreet_icache` | `s1_compile` | 1 | internal | `read_slang` failed on the icache filelist |
| B04 `cva6_mmu_tlb_ptw` | `s1_compile` | 6 | syntax | combinational loop in `cva6_mmu_gate` |
| B05 `coreet_etlink_axi` | `s1_compile` | 1 | internal | `read_slang` failed on `shirecache_mesh_master` |
| B05b `coreet_etlink_axiport` | — | — | — | **no attempt recorded at all** |
| B06 `cva6_lsu` | `s1_compile` | 6 | syntax | combinational loop in `load_store_unit_gate` |
| B07 `coreet_vpu` | `s1_compile` | 6 | syntax | combinational loop in `vpu_top` |
| B08 `coreet_intpipe` | `s1_compile` | 6 | syntax | combinational loop in `intpipe_top` |
| B11 `coreet_tlb` | `s2_single_edge` | 7 | unsupported | `memory memory_1660:u_tlb_cache_rf.rf_q would commit on every sub-step under a phase divider` |

Three groupings, and they are not the same problem:

1. **Front-end (B03, B05)** — `read_slang` does not get as far as an LGraph.
2. **Combinational loops (B02, B04, B06, B07, B08)** — five units, all reported
   as `syntax`/exit 6 at `s1_compile`.  Four of the five chains are
   `sext -> get_mask -> sra -> sext -> …`.  That is the SHL/SRA/Sext area, and
   the repairs there are **not** to be rolled back; whether these are real
   loops or an artefact of how the loop detector walks those chains is an open
   question and is NOT answered here.
3. **Structural (B01)** — a concat lane driver wider than its declared window.

**B11 is the only one blocked inside the memory area, and it is the closest to
the gate it fails.** It passes `s1_compile` and is refused by
`pass.single_edge`, whose own hint names the gap:

```
code: memory-unsupported
hint: slot enables are not wired into the Memory cell yet
```

So B11 is an implementation gap in the normalization pass — a memory whose
commit cannot currently be qualified by a slot enable under a phase divider —
and not a statement that the design cannot be certified.  It is recorded here
as the next candidate in this area; nothing is claimed for it yet, and no
component is erased to get past it.

Matrix stays **12 YES / 11 NO / 37 BLOCKED**.

## §63.55 — whole Pico, measured; the check metric; fresh source-bound emission

### The double specialization, measured away

Same certificate, same fuel, the only change being `mkMemSimOf`:

| phase | before | after |
|---|---|---|
| specialize | 676,932 ms | 949,243 ms |
| **check** | **659,085 ms** | **2,590 ms** |
| execute | 756,889 ms (4 cycles) | 1,048,206 ms (6 cycles) |

The "check" was a second full specialization.  It is now a check: **659 s → 2.6 s**.
(The specialize figures are two different runs of the same work, not a
regression claim; the point is the check column.)

### Whole PicoRV32, on the retained normalized certificate

`vc_opt/lean/picorv32.dcert`, 70,067 B, sha256 `f85a2a9d…`:

```
shape: sources 1302 nodes 2279 outputs 18 flops 174 memories 1 clocks 1
memory 0: aw 5 dw 32 nextImg slot 3225 clock 0 (table 32 words)
memory route: accepts true typed true sized true nextImgs true | ADMITTED true
residual 4,670,254 terms, 1 function(s), arity [3], checker bound 99,236
step outcomes: ok,ok,ok,ok,ok,ok
reference comparison: COMPLETE (array length, every table word, default and out-of-range)
final returned-state policy: stateFullOKB true
```

producer exit 0; 34:52 wall, 1956.9 s user + 15.9 s sys, peak RSS 6,965,844 KiB.

The SUPPLIED image survives the whole route — the file's nonzero default
`4294967295` and its words are intact at the end:
`[19088743,2309737967,305419896,2882400001,4294967295,4294967295]`, and the
`encStateM`/`decStateM` round trip was checked, not assumed.  The file's
`flops reset` directive is reported as the synthetic part it is.

**What did NOT happen, stated plainly: `memory 0: 0 word(s) differ`.**  Under
the generated width-derived stimulus the register file never commits, so this
run has NO memory transition.  The driver reports that rather than describing
motion that did not occur.  A `stim` file was added for exactly this reason —
`mkInputFor` cannot drive a core that needs `resetn` deasserted and a ready
memory interface — and a run with `resetn = 1`, `mem_ready = 1` and
`mem_rdata = 0x00100093` (`addi x1, x0, 1`, which writes the 32×32 register
file this certificate declares as its aw5/dw32 memory) is the one that can
produce a transition.

### Fresh emission, from a source-bound build

`bazel build -c opt //lhd:lhd` at repo HEAD `1b180a422`: 10,279 actions,
completed successfully.  New binary sha256
`9ea9245ee4e3f8ac01176bcf9be003b04212a2abbb1f809b0487732a8d798a97` — **not**
the `414d5dcb…` of §63.51, whose missing `.dcert` sidecar was the symptom.

Re-emitted from the MATCHED `s2_single_edge` LGraph with the same
`formal.lean.mode=verified_compiler` invocation `vc_opt` used: producer exit 0,
0.98 s, 77,076 KiB, and it **does** emit the sidecar.

| certificate | bytes | sha256 | sources | nodes | flops | memory | admitted |
|---|---|---|---|---|---|---|---|
| retained `vc_opt` | 70,067 | `f85a2a9d…` | 1302 | 2279 | 174 | aw5 dw32 | **yes** |
| fresh, current emitter | 69,657 | `ef77dfab…` | 1292 | 2269 | 174 | aw5 dw32 | **yes** |

They are NOT byte-identical and are not claimed to be: the current emitter
produces ten fewer sources and ten fewer nodes.  Both are retained separately,
and both are admitted by the memory route with the same memory geometry.

### Driver additions

* `stim:<path>` — a SUPPLIED input vector (`stim v1` / `in <ordinal> <width>
  <value>`), applied every cycle, provenance and hash reported; unlisted
  ordinals keep their declared width at 0.  The generated stimulus remains the
  default and is labelled synthetic.
* `support-only` — stop after the support report, so two certificates'
  admission can be compared without paying for specialization.

Matrix stays **12 YES / 11 NO / 37 BLOCKED**.

## §63.56 — B11's blocker, assessed: no new IR component is needed

`pass/single_edge/pass_single_edge.cpp:901` refuses any design containing a
`Ntype_op::Memory` once the slot plan needs more than one slot:

```cpp
if (op == Ntype_op::Memory && plan.slots > 1) {
  r.reason = "memory `" + label_of(n) + "` would commit on every sub-step under a phase divider";
  refuse(quiet, "memory-unsupported", …, "slot enables are not wired into the Memory cell yet");
```

The pass fails closed on purpose — its own comment is *"edge normalization
declines rather than half-transform: a partial lowering is a silent full-cycle
error"* — so the refusal is correct behaviour for a gap, not a bug to delete.

### What the gap is, and what it is NOT

It is a **lowering** gap: under a phase divider the Memory cell has no
slot-qualified commit, so the pass cannot guarantee the write lands only on its
own sub-step.

It is **not** a certificate-language gap, and this is worth recording because
the obvious assumption is the opposite.  DCERT2 and the proofs already express
a memory that commits on an edge WITHOUT changing: a byte-enable of zero leaves
the image EXACTLY alone, proved, not sampled —

* `MemImg.writeBE_disabled_eq : ¬ bv_nonzero bev → writeBE w m a d bev bw = m`
* `applyOpM_memWriteBE_disabled` — the same at the object level
* `memNextOf_quiet` — the prior image, exactly, when the clock does not fire

and the executed fixture control (`inNoWr` / the `mask 0` rows of §63.49) shows
the image unchanged on a FIRING edge with the write disabled.

So a slot-gated memory write is expressible today by ANDing the slot predicate
into the existing byte-enable operand: no new `MemoryDesc` field, no new
component on the shared `DesignCert`, and no change to the memory semantics
already proved.  The work is in `pass.single_edge`'s lowering, where the slot
machinery lives (`plan.slots`, the per-element `e.slot` assignment at
`pass_single_edge.cpp:613-641`), and it is NOT attempted here: the pass assigns
slots by retyping elements onto slot-specific clocks rather than by building an
enable predicate, so the right shape of the change needs that machinery
understood rather than guessed at.  A half-understood edit to a deliberately
fail-closed pass is the one thing its comment warns against.

Recorded as the next concrete candidate in the memory area, with the finding
that it does not require extending the IR.  B11 stays BLOCKED; nothing is
claimed for it, and no component is erased.
