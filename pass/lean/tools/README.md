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
| `HDLProof/Sym/` | the verified symbolic step of a design on three routes (`sim:`): fp's residual (`Eval`, `Step`; its primitives in `Prim` and `Dispatch`, with `IntDiv` (Euclidean `divI` / `modI`), `EqV` (`eqV`) and `CtorP` (the constructor primitives)), the certificate's nodes and the verified compiler's bindings (`NativeOps`, `NativeBuilders`, `Native`, `NativeInterp`, `NativeRoute`: `irStepper_sound`, `vcStepper_sound`); SAT/LRAT (`Sat`), k-induction (`KInd`), conjunctive invariants (`Conj`, `ConjT`); the closed checks on every route (`Closed`: `Sim`, `Sim.stepper_sound`, `runInvKOn`); `BvLit` (the equality and one-bit literals the refinement-map obligations use); `Diag` (untrusted: operator census and a counting copy of fp's step, for `sym_probe`) |
| `HDLProof/Kit/` | `Norm` (normalized memory-free states, state values from leaves), `BitE` (formulas and observed words over a design's real ports), `Driver` (the drivers' shared IO helpers; untrusted, no theorems) |
| `HDLProof/Check/RefMap*.lean` | the refinement-map checker: a design refines a spec certificate with stuttering, the spec's state at every cycle being the map's value (`RefMap`, `RefMapSound`: `runRefMapOf`, `runRefMapOf_sound`) |
| `HDLProof/Check/SpecFacts.lean` | the spec's facts, checked once per spec: Moore, and confluence of every pair of channels (`runSpecFactsOf_sound`) |
| `HDLProof/Check/RefMapCompose.lean`, `RefMapRef.lean` | two checked maps of one spec show the same streams (`refmap_compose`); progress (`refmap_progress`); both restated over `interpretDesign` for verdicts of any route (`refmap_compose_any`, `start_any`, `refmap_progress_any`, over `RefMapPasses` / `SpecFactsPass`), which every verdict form gives (`RefMapChecked` / `SpecFactsChecked` on fp, `RefMapCheckedOn` / `SpecFactsCheckedOn` on any route, `.passes`); `refmap_compose_ref`, `start_ref`, `refmap_progress_ref` take the fp forms |
| `HDLProof/Check/Audit.lean` | the axiom gate: `#audit_axioms` for every checker theorem; the build fails on anything but `propext`, `Classical.choice`, `Quot.sound` |
| `drivers/` | `sym_verify.lean` (invariants, k-induction, `conj:` specs), `sym_refmap.lean` (refinement with stuttering by refinement maps, spec facts, the judge; `.refmap` files; theorem files); both take `sim:fp\|ir\|vc`, default `vc`; `sym_probe.lean` (one symbolic step without SAT on each route: OK or the refusal's class, with operator and primitive counts) |
| `formats/` | `refmap.md`: the `.refmap` format, what each target means, results, theorem files |
| `tests/` | `sym_selftest.sh` and `refmap_selftest.sh`: small fixtures through the real flow, passing and failing; `native_symbolic.lean` and `fp_symbolic.lean`: each route's symbolic operators and primitives against the concrete semantics on many assignments; `gen_gap_certs.lean`: certificates no front end emits (symbolic masks, a whole-memory load) |
| `scripts/` | `sym_observe.py`: adds observer outputs (witnesses, internal state) to a copy of the RTL; `sym_survey.py`: runs `sym_probe` on a list of certificates under a memory cap and tabulates the refusals |

## Build

The Lean pin (4.36.0-rc1) lives in `formal/lean`, so build from there:

```bash
cd formal/lean
bash .build-proj.sh                       # simulator + tools, into .olean-dev, with the axiom gate
bash .build-proj.sh HDLProof/Sym/ConjT    # one tools module
# native drivers, into a project-local root (incremental: only modules whose source or imports
# changed are rebuilt; FORCE=1 rebuilds all; JOBS=<n> parallel C compiles, default 4):
OPT=-O2 ROOT=$PWD/../../generated/sym/nat_root bash Futamura_Projection_Scripts/build-opt.sh \
    ../../pass/lean/tools/drivers/sym_verify.lean ../../pass/lean/tools/drivers/sym_refmap.lean \
    ../../pass/lean/tools/drivers/sym_probe.lean
ln -s "$(dirname "$(elan which lean)")/cadical" ../../generated/sym/nat_root/nat/cadical
```

Self-test, natively or interpreted against `.olean-dev`:

```bash
SYM_NAT=<root>/nat LHD=<lhd> bash pass/lean/tools/tests/sym_selftest.sh
SYM_NAT=<root>/nat LHD=<lhd> bash pass/lean/tools/tests/refmap_selftest.sh
```

A native driver looks for `cadical` beside itself unless `solver:` is given.

## Use

`pass/lean/README.md`, section "Unbounded proofs", covers:
- exporting a design;
- the property and `conj:` options, and refinement maps (`sym_refmap`);
- the counterexample files an agent iterates on.

`pass/lean/specialization/M3_UNBOUNDED_PROOFS.md` covers what a verdict means, what is trusted, and
what each route evaluates symbolically and refuses.

`formats/refmap.md` covers refinement maps. An agent proving that two designs are equivalent
through a common spec does this:
1. Write the spec certificate.
2. Prove its facts: `sym_refmap specfacts`.
3. For each design, add observer outputs for the map, the rank and the selector, and write a
   `.refmap`.
4. Iterate on `sym_refmap refmap`, reading `cti_<target>.txt`, until REFINEMENT PROVED.
5. Emit the verdicts (`emit:`) and compose them with `Check.refmap_compose_ref`.
