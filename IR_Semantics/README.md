# IR_Semantics — the Direction-2 (IR-as-semantics) working tree

What this direction owns that is NOT shared with the rest of LiveHD: the
corpus it measures, the measurements themselves, and the tooling that produces
them. The implementation is deliberately **not** here — see "What stayed, and
why" below.

```text
IR_Semantics/
  manifests/   the corpora, and the per-module parameters needed to elaborate
               them (MODULE_PARAMS.tsv, coreet_*.tsv, cva6_*.tsv, PROVENANCE)
  evidence/    what was measured: censuses, sweeps, the gated-module ledger,
               and the write-ups of individual findings
  scripts/     the tooling that produces all of it: the CORE-ET flow
               (run_coreet_module_lean.sh and the coreet_* helpers), the
               coverage ledger, and the RTL-vs-regenerated differential
```

## The headline numbers live in two files

* `evidence/CENSUS_MASTER_122.tsv` — the corpus measured on master's DEFAULT
  pipeline. Its `lowering` column records the engine, because that changes the
  answer: `ifx+aegraph` is 109 of 122, and `--set cprop.engine=classic` is 112
  (`evidence/AEGRAPH_EXPORT_REGRESSION.md`).
* `evidence/CENSUS_D2_122.tsv` — the older run on the `direction-2` branch, at
  a commit that is not in master's history. Kept as the historical baseline;
  do not quote it as a master number.

Regenerate the ledger with:

    IR_Semantics/scripts/coverage_ledger.py --base IR_Semantics/evidence/CENSUS_MASTER_122.tsv

## What stayed, and why

This folder holds no compiled code, on purpose. The IR-as-semantics
implementation is **shared infrastructure**, consumed by the D3, D4 and
Futamura-projection directions as well as this one, so lifting it here would
break them:

| stays | where | why |
|---|---|---|
| the exporter (LGraph → `DesignCert`) | `pass/lean/*.cpp\|hpp` | 3,598 targets outside `pass/lean` depend on it |
| the Lean semantics and its proofs | `formal/lean/LeanSemanticPrimitives/Compiler/` | one Lake package, pinned toolchain, Mathlib dep; 17 of its 33 modules are imported from outside it, `CompileDesign` 13 times |
| the proof tools | `pass/lean/tools/` | upstream's HDLProof engine, actively developed |
| the bazel test data | `pass/lean/tests/` | wired into test targets |

`pass/lean/README.md` documents the exporter; the Lean layers are
`DesignSemantics` (`interpretDesign`, the reference), `DirectSemantics` /
`DirectTrace` (the checked stepper) and `CompileDesign*` (the compiled path),
tied together by `DirectVsCompiled`.

## The boundary this direction does not close

Those three Lean layers are proved equal to one another, at step and at trace
level, with no axioms and no `sorry`. **Nothing proves the `DesignCert` is the
RTL.** That edge is the C++ exporter plus the LGraph, and it is policed only by
the differential in `scripts/v2v_verilator_diff.py` — which is why the masking
defect recorded in `scripts/V2V_INPUT_MASKING.md` mattered more than its size
suggests.
