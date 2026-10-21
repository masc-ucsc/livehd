# HDLProof: proof tools for LiveHD designs

These tools let an agent prove properties of a design and check the proofs. Each checker is proved
sound once, in Lean, for every design. A run for one design only builds obligations, hands them to
an untrusted SAT solver, and checks the LRAT proofs it returns. No per-design Lean proof is
written.

The design enters as a LiveHD certificate (`.dcert` plus its `_io.json` port and flop names). The
checkers work on the residual of the Futamura projection of the simulator in
`formal/lean/LeanSemanticPrimitives/Projection`. That library (specializer, DesignCert semantics,
memory route) stays in `formal/lean`; everything here builds on it.

## Layout

| path | contents |
|---|---|
| `HDLProof/Theory/` | refinement theory in pure Lean, independent of hardware: channel specs and Kahn determinism (`Channels`), runs refining a spec with stuttering and the `PipelineRefinement` API (`Stutter`), horizontal and vertical composition (`Compose`), progress under fairness (`Progress`) |
| `HDLProof/Sym/` | the verified symbolic interpretation of a residual: evaluator (`Eval`), SAT/LRAT (`Sat`), k-induction (`KInd`), conjunctive invariants (`Conj`, `ConjT`), refinement with stuttering (`Refine`) |
| `HDLProof/Kit/` | `Norm` (normalized memory-free states, state values from leaves), `BitE` (formulas and observed words over a design's real ports), `Driver` (the drivers' shared IO helpers; untrusted, no theorems) |
| `HDLProof/Check/RefMap*.lean` | the refinement-map checker: a design refines a spec certificate with stuttering, the spec's state at every cycle being the map's value (`RefMap`, `RefMapSound`: `runRefMapOf`, `runRefMapOf_sound`) |
| `HDLProof/Check/SpecFacts.lean` | the spec's facts, checked once per spec: Moore, and confluence of every pair of channels (`runSpecFactsOf_sound`) |
| `HDLProof/Check/RefMapCompose.lean`, `RefMapRef.lean` | two checked maps of one spec show the same streams (`refmap_compose`); progress (`refmap_progress`); both restated over `interpretDesign` (`refmap_compose_ref`, `start_ref`, `refmap_progress_ref`) for verdicts stated as `RefMapChecked` / `SpecFactsChecked` |
| `HDLProof/Check/Audit.lean` | the axiom gate: `#audit_axioms` for every checker theorem; the build fails on anything but `propext`, `Classical.choice`, `Quot.sound` |
| `drivers/` | `sym_verify.lean` (invariants, k-induction, `conj:` specs), `sym_refine.lean` (refinement with stuttering, output pairs), `sym_refmap.lean` (refinement maps and spec facts; `.refmap` files; theorem files) |
| `formats/` | `refmap.md`: the `.refmap` format, what each target means, results, theorem files |
| `tests/` | `sym_selftest.sh` and `refmap_selftest.sh`: small fixtures through the real flow, passing and failing |
| `scripts/` | `sym_observe.py`: adds observer outputs (witnesses, internal state) to a copy of the RTL |

## Build

The Lean pin (4.31.0) lives in `formal/lean`, so build from there:

```bash
cd formal/lean
bash .build-proj.sh                       # simulator + tools, into .olean-dev, with the axiom gate
bash .build-proj.sh HDLProof/Sym/ConjT    # one tools module
# native drivers, into a project-local root:
OPT=-O2 ROOT=$PWD/../../generated/sym/nat_root bash scripts/build-opt.sh \
    ../../pass/lean/tools/drivers/sym_verify.lean ../../pass/lean/tools/drivers/sym_refine.lean \
    ../../pass/lean/tools/drivers/sym_refmap.lean
ln -s "$(dirname "$(elan which lean)")/cadical" ../../generated/sym/nat_root/nat/cadical
```

Self-test, natively or interpreted against `.olean-dev`:

```bash
SYM_NAT=<root>/nat LHD=<lhd> bash pass/lean/tools/tests/sym_selftest.sh
SYM_NAT=<root>/nat LHD=<lhd> bash pass/lean/tools/tests/refmap_selftest.sh
```

A native driver looks for `cadical` beside itself unless `solver:` is given.

## Use

`pass/lean/README.md`, section "Unbounded proofs on the projection simulator", covers:
- exporting a design;
- the property, `conj:` and `sym_refine` options;
- the counterexample files an agent iterates on.

`pass/lean/specialization/M3_UNBOUNDED_PROOFS.md` covers what a verdict means and what is trusted.

`formats/refmap.md` covers refinement maps. An agent proving that two designs are equivalent
through a common spec does this:
1. Write the spec certificate.
2. Prove its facts: `sym_refmap specfacts`.
3. For each design, add observer outputs for the map, the rank and the selector, and write a
   `.refmap`.
4. Iterate on `sym_refmap refmap`, reading `cti_<target>.txt`, until REFINEMENT PROVED.
5. Emit the verdicts (`emit:`) and compose them with `Check.refmap_compose_ref`.
