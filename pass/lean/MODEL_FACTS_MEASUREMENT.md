# Moving the MODEL facts into modules: the measurement before the change

Written BEFORE implementing, so the acceptance numbers are not chosen to fit
whatever the first run produces. Same discipline as `MODULE_PROOF_CANARY.md`.

## What the phase data established

`--phase-markers` on the composition (commit 117fcffd5):

    design        bindings   elaboration   audit       audit share
    txfma_e6           133        0.74 s    6.99 s         90%    PROVED
    serdiv_gate       1180        7.0  s   >90 min       >99.8%   undecided

Proof CONSTRUCTION is free. `serdiv_gate` reaches `correct` in 7.0 seconds --
all 37 `chunk_eq` at 0.0-0.2 s each -- and then never leaves `audit_start`.
The cost is `collectAxioms` forcing the kernel to check what elaboration only
dispatched.

The module backend already removed the COMPILER facts from that audit: an
imported theorem's body was checked when its module was built, so auditing it
costs ~7 MB (measured: 2,027,928 kB to import and audit two, against a
2,020,500 kB bare-import floor). The MODEL facts were never moved, and they
are what remains local.

## What is still local, exactly

Per segment `j`, `prove_reified_chunked` emits into the composition:

    def     seg<j>    : List Compiler.ResidualBinding := <literal, <=32 bindings>
    theorem chunk_eq<j> (e : SlotEnv) : chk<j> e = runBindings seg<j> e := by
      simp only [chk<j>, seg<j>, runBindings, denoteExpr, refBVs, ...]

plus the prefix chain, `segs_eq`, `env_eq`, and `d3_fast` itself.

**The change is larger than "move chunk_eq".** `chunk_eq<j>` mentions
`chk<j>`, a chunk function of the MODEL, emitted in-process by
`reify_design_chunked`. A theorem cannot be imported while the function it
mentions is still generated locally, so the model emission has to move into
generated modules as well. That makes this a change to `d3_gen_modules.py`
and the reifier, not a one-line prover edit -- which is exactly why it gets
measured first.

## The measurement (cheap, decisive, no restructure)

Mirror the compiler-facts measurement that justified the module backend.
On ONE small design (`txfma_e6`, 133 bindings, 5 chunks), compare three
compositions and record cgroup CHARGE, max RSS, and audit wall from
`audit_start` to `audit_done`:

  A. baseline      model local, compiler facts imported  (today's path)
  B. model-in-mod  model chunk functions AND their chunk_eq built as a module,
                   imported by the composition
  C. floor         import both modules, audit a trivial theorem -- the cost of
                   importing at all, so B is read against the right zero

## Acceptance -- what would justify the restructure

1. B's audit wall and charge are materially below A's. "Materially" is set
   BEFORE the run as **>=3x** on audit wall, which is the margin the
   compiler-facts move produced (2,396,532 kB to build a module vs ~7 MB to
   audit an imported theorem).
2. B still gates `proof=1` with `thm=d3_fast.correct` and axioms exactly
   [propext, Classical.choice, Quot.sound]. A cheaper audit that audits less
   is not the goal.
3. C shows the saving is not simply import cost moved around.

If B is within noise of A, the restructure is NOT justified by this evidence
and the model facts are not where the audit cost lives -- in which case the
next question is the prefix chain and `d3_fast` itself, which this
measurement deliberately leaves local in B.

## What this measurement does NOT settle

  * It says nothing about whether `serdiv_gate` would then converge. serdiv's
    audit is an order of magnitude worse than `commit_stage_gate`'s at a
    similar binding count and the phase data does not explain that spread.
  * One small design is evidence about one small design. `txfma_e6` audits in
    6.99 s; a 3x there is not a promise of 3x at 1,180 bindings.
  * Earlier hypotheses about serdiv (Op_Xor, Op_MuxBool volume) are about
    ELABORATION, which is 7 seconds. They are hypotheses about the wrong
    phase and are not revived by this work.

## Result of a cheaper arm tried first (D), and a design error in it

Before arm B, a cheaper idea was tested: if the audit is expensive because
`collectAxioms` forces the whole dependency graph at once, maybe simply
BUILDING the composition as a module -- where checking happens per
declaration at build time -- is the whole fix, with no restructure at all.

    design              arm A (one file)        arm D1 (one module)
    txfma_e6   (133)    9.88 s /   664,544 kB   10.52 s /   685,084 kB
    serdiv_gate(1180)   TIMEOUT 2400 s /        TIMEOUT 2700 s /
                        12,207,062 kB            13,718,216 kB, no olean

    arm D2 (import that module, audit the imported theorem):
                        2.27 s / 292,148 kB  -- near the bare-import floor

REFUTED. Building the composition as a module does not help: serdiv fails
that way too, at HIGHER charge. Relocating the kernel check to build time
does not reduce it. D2 does confirm the import mechanism itself is cheap, so
the mechanism is sound and the problem is elsewhere.

**Arm D1 was poorly designed and the error is worth recording.** It put the
entire model in ONE module. The compiler facts are not organised that way --
they are split across TEN modules, and that is what makes them cheap:

    ariane_regfile_gate  10 compiler-fact modules   619,844 - 2,228,880 kB each
                         composition, model local        12,267,132 kB

So D1 tested MODULE-NESS when the property that actually worked is SPLITTING.
It refutes "put the whole model in one module". It does NOT refute arm B,
which is the split, and the table above is the positive evidence for arm B:
the same facts cost 0.6-2.2 GB apiece when divided across ten modules and
12.3 GB when checked together.

Arm B therefore stands, with its acceptance numbers unchanged, and with one
addition learned here: B must split the model facts across MANY modules on
the same grouping the compiler facts already use (`--per-group`, default 4
chunks per module), not into a single model module. A single-module arm is
already answered -- it is D1, and it fails.
