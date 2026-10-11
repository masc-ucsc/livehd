# Generation-only procedure for the covered backend

**Simulator generation latency is a standalone metric.** This procedure measures
it and nothing else. It is not a reference-validation run, it does not produce a
benchmark pass, and its success is named differently so the two can never be
confused.

## What the driver does

`gen_probe` takes `<cert.dcert> <stepFuel> <workFuel>` and stops after
generation and checking:

1. `loadCert` — certificate parse
2. `CertLoad.supportReport` — all six `SupportedByProjection` fields; a refusal
   here exits 2 and is reported as `GENERATION REFUSED (unsupported)`
3. `mixDriver sf wf hwAPVarT [encDesign D]` — the **PROVED** specializer with the
   **TOTAL** variant interpreter, forced only to WHNF
4. `Hw.checkResidual` — the fragment checker, which traverses the residual

**End-to-end generation = 1 + 2 + 3 + 4.** Nothing else is inside that interval.

It never runs reference stimuli, the threaded trace or the control interpreter.
`total_probe` and its admission contract are untouched; the full benchmark mode
still exists and still means what it meant.

Outcomes: `GENERATION OK` (exit 0), `GENERATION CHECK REJECTED` (1),
`GENERATION REFUSED (unsupported)` / `GENERATION FAILED` (2),
`GENERATION OUT OF FUEL` (3). **`GENERATION OK` is weaker than a benchmark
pass and must never be entered as one.**

## What is deliberately outside the measured interval

`szOf`, the full term-count traversal, runs **after** the interval and is
reported on its own line as `EXCLUDED FROM GENERATION -- profiling`.

This matters more than it sounds. `total_probe` measures stages with
`Projection.Runner.stage`, which calls its digest function *inside* the timed
region:

```lean
let t0 ← IO.monoMsNow
let v ← IO.lazyPure thunk
let d ← IO.lazyPure (fun _ => digest v)   -- a full szOf traversal
...
let t1 ← IO.monoMsNow
```

so **every `specialize` and `specialize_and_check` number ever recorded by
`total_probe` includes a full `szOf` traversal of the residual.** Any comparison
against a remembered generation time has to account for that before it can mean
anything.

## The artifact

What generation produces is an **in-memory `Program`** -- a Lean value in the
running process. Nothing is serialized and no source file is written. It is not
raw native-emitter source (`pass.lean`'s `<Top>_Lgraph.lean`) and must never be
reported as such. If a serialization or emission step is ever added, it gets its
own timed phase.

## Proof accounting for this path

`specializeDesign_varT_correct` and `simSound_varT`
(`Projection/Proto/VariantAdequacy.lean:243` and `:285`) are **universal
conditional** results. Their premises, and what a generation-only run does with
each:

| premise | status here |
|---|---|
| `hsup : SupportedByProjection D` | **PROVED FROM A BOOLEAN** -- `CertLoad.supported_of_report` gives `(supportReport D).allOK = true → SupportedByProjection D`. The run **observes** that Boolean true by native execution; it is not kernel-reduced at these sizes. |
| `hchk : checkResidual R = some b` | **OBSERVED** by native execution. Not kernel-reduced. |
| `hproj : mixDriver sf wf hwAPVarT [encDesign D] = .ok R` | **TRUSTED.** `mixDriver` does not kernel-reduce at nontrivial fuel, so the concrete residual carries no kernel-checked `hproj` evidence. |
| `hwf : RuntimeWF D i s`, `hrs : RuntimeSized D e i s` | **NOT REACHED** -- generation supplies no stimuli. |

Nothing here discharges a universal premise, and no proof flag is claimed. A
generated residual is **not** kernel-certified merely because the native checks
succeeded. Reference comparisons, when they run, are **regression evidence**;
they do not discharge `hproj` either, so they are kept out of ordinary
generation.

## The two builds

The same driver source is built twice, into isolated project-local roots, from
two trees that both pin Lean `v4.31.0` (verified: both resolve
`4.31.0 … 68218e876d2a38b1`) and use the same `leanc` (clang 22.1.4) at `-O2`:

* **pin** — the exact FP pin `9bb41a09d6b57111ed2b3fd10102ec4ff174b6c6`,
  extracted to `generated/master_port/fp/genprobe/pin_tree/`
* **master** — this worktree's `formal/lean`

The driver differs between the two sides by **exactly two lines**, both marked
`ADAPTATION`, because the pin's DCERT1 loader is `Compiler.CertIO` and master's
is `Compiler.CertIOV1`:

```
41c41
<  import LeanSemanticPrimitives.Compiler.CertIOV1   -- ADAPTATION(master)
>  import LeanSemanticPrimitives.Compiler.CertIO     -- ADAPTATION(pin)
53c53
<  let D ← CertIO.V1.loadCert path                   -- ADAPTATION(master)
>  let D ← CertIO.loadCert path                      -- ADAPTATION(pin)
```

| file | sha256 |
|---|---|
| `gen_probe.master.lean` | `b6dff38b6885b0d8559096374670bd907b84c883580624bef8acb27f22099f2c` |
| `gen_probe.pin.lean` | `26b88f106c5f097b4ebef802e4b578cd0b6fcae8965299e7f4c14d3dac0db2bd` |

The two `scripts/build-opt.sh` MODULES lists differ only in that same
`Compiler/CertIO` vs `Compiler/CertIOV1` entry.

**The existing `.native-dev/total_probe` is not a pin baseline.** It is a
captured older dirty build at `git_rev 76e1fc955…`, and the manifest row for it
carries no stage times at all. Its timings are not attributed to the pin; the
pin is rebuilt instead.
