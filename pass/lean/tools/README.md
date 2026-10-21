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
| `HDLProof/Sym/` | the verified symbolic interpretation of a residual: evaluator (`Eval`), SAT/LRAT (`Sat`), k-induction (`KInd`), conjunctive invariants (`Conj`, `ConjT`), refinement with stuttering (`Refine`) |
| `HDLProof/Check/Audit.lean` | the axiom gate: `#audit_axioms` for every checker theorem; the build fails on anything but `propext`, `Classical.choice`, `Quot.sound` |
| `drivers/` | `sym_verify.lean` (invariants, k-induction, `conj:` specs), `sym_refine.lean` (refinement with stuttering) |
| `tests/` | `sym_selftest.sh`: small fixtures through the real flow |
| `scripts/` | `sym_observe.py`: adds observer outputs (witnesses, internal state) to a copy of the RTL |

## Build

The Lean pin (4.31.0) lives in `formal/lean`, so build from there:

```bash
cd formal/lean
bash .build-proj.sh                       # simulator + tools, into .olean-dev, with the axiom gate
bash .build-proj.sh HDLProof/Sym/ConjT    # one tools module
# native drivers, into a project-local root:
OPT=-O2 ROOT=$PWD/../../generated/sym/nat_root bash scripts/build-opt.sh \
    ../../pass/lean/tools/drivers/sym_verify.lean ../../pass/lean/tools/drivers/sym_refine.lean
```

Self-test, natively or interpreted against `.olean-dev`:

```bash
SYM_NAT=<root>/nat LHD=<lhd> bash pass/lean/tools/tests/sym_selftest.sh
```

## Use

`pass/lean/README.md`, section "Unbounded proofs on the projection simulator", covers:
- exporting a design;
- the property, `conj:` and `sym_refine` options;
- the counterexample files an agent iterates on.

`pass/lean/specialization/M3_UNBOUNDED_PROOFS.md` covers what a verdict means and what is trusted.
