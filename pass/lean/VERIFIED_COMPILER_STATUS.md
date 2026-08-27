# B1+B2 verified compiler — what is built

Branch `b1-b2-verified-compiler`, cut from `interpreter-value-polymorphic`
(`827e53233`).  Plan: `/soe/czeng14/projects/hagent/b1-b2-plan.md`.

## Review status (2026-08-27)

All three DINO certificates were regenerated with
`formal.lean.mode=verified_compiler` and checked serially after explicitly
building `Compiler.CompileDesign` (so stale `.olean` files cannot mask a source
failure):

| design | nodes | wall | peak RSS | result |
|---|---:|---:|---:|---|
| `SingleCycleCPU` | 4,772 | 45.54 s | 7.38 GB | proven |
| `PipelinedCPU` | 5,061 | 49.52 s | 7.44 GB | proven |
| `PipelinedDualIssueCPU` | 10,740 | 160.18 s | 8.34 GB | proven |

The old hour-scale behavior is not current compiler evaluation.  It came from
the obsolete theorem shape that named a concrete `ResidualProgram`, forcing an
O(N^2) kernel reduction, or from accidentally running the legacy emitter.  The
DINO runner now defaults to `LEAN_MODE=verified_compiler`; use
`LEAN_MODE=legacy` only for differential comparison.

The review also hardened the C++ trust boundary: duplicate sparse IDs and
missing source ordinals are fatal instead of silently mapping to dense slot or
runtime ordinal zero.  Synchronous-read memories are now rejected in verified
compiler mode until their registered read-data sources and next-state updates
are represented explicitly in `DesignCert`.

## The theorem

```lean
theorem compileDesign_correct (D : DesignCert) (R : ResidualProgram)
    (hc : compileDesign D = .ok R) :
    ∀ (inp : RuntimeInput) (st : RuntimeState),
      denoteResidual R inp st = interpretDesign D inp st
```

Proved. `#print axioms` → `[propext, Classical.choice, Quot.sound]`; no `sorry`.

**Two hypotheses the plan expected are absent.**

* No `DesignCertWF D`. `compileDesign` *checks* dependency-ordering itself
  (`firstBadDep`) and refuses unsupported operators, wrong arities and zero
  widths (`compileOp`), so `hc` witnesses everything needed for semantic
  equality. It does not yet reject every malformed shell reference: out-of-range
  output/flop/memory slots are totalized identically by source and target
  semantics. Add a shell-reference check before treating compiler acceptance as
  a complete certificate well-formedness check.
* No `RuntimeWF D inp st`. Neither side's output, flop or memory rule depends on
  the state's size, so no runtime shape condition is required. (`RuntimeWF` is
  defined in `DesignSemantics.lean` and went unused — kept only as documentation
  of what was checked and found unnecessary.)

This is a strong semantic-equivalence theorem, but acceptance and complete
hardware-certificate well-formedness are distinct claims.

## Files

| file | lines | Mathlib? | contents |
|---|---:|---|---|
| `Compiler/DesignCert.lean` | 157 | no | dense slot space, `toGraphCert` |
| `Compiler/DesignCertWF.lean` | 133 | no | `DepsBounded` ⇒ all three structural facts |
| `Compiler/Runtime.lean` | 60 | no | `RuntimeInput/State/Result`, `SlotEnv`, `denoteRef` |
| `Compiler/ResidualIR.lean` | 145 | no | target language, `ValueType`, `CompileError` |
| `Compiler/ResidualSemantics.lean` | 205 | no | `denoteExpr`, `denoteResidual`, `flopNext` |
| `Compiler/DesignSemantics.lean` | 77 | no | `interpretDesign`, `srcFlopNext` |
| `Compiler/CompileOp.lean` | 400 | yes | `compileOp` + 24 value lemmas + `compileOp_correct` |
| `Compiler/CompileGraph.lean` | 190 | yes | `runBindings` lemmas, `compileGraph`, its spec |
| `Compiler/CompileDesign.lean` | 300 | yes | `slot_agree`, `compileDesign`, the theorem |
| `pass/lean/design_cert_export.hpp` | 264 | — | LGraph-free remap + formatter |

Only the proof files import Mathlib; the definitions a generated design
instantiates do not, matching `GraphRefine`'s existing split.

## Four results worth stating separately

### 1. Dense indices replace three `native_decide` gates with one bounds check

The manual path discharges `topo.Nodup`, `DepOrdered G topo` and `isSome` on
topo per design, by `native_decide` over list membership.  With sources at slots
`0..S-1` and node `i` at `S+i`:

* `topo_nodup` and `wf_isSome` hold **unconditionally** — facts about `slotsFrom`
  and `Array.getElem?`, proved once;
* `DepOrdered` follows from the single arithmetic condition
  `DepsBounded : d < S + i` (`wf_depOrdered`).

### 2. Eight operator refusals are measured, not guessed

Census over all 123 generated `*_Lgraph.lean` files.  Of the nine operators with
zero `OpBridge` coverage, **eight appear nowhere** and get `.unsupportedOp`:

```
Op_Const  Op_Sub  Op_Div  Op_UDiv  Op_SDiv  Op_LT  Op_GT  Op_SetMask
```

`Op_Const` is absent as a node op because constants are certificate SOURCES;
`Op_Sub`/`Op_LT`/`Op_GT` are normalised away by cprop.  Only `Op_Mult` (12
nodes, `tima_top`) was reachable and needed a genuinely new lemma.  A refusal is
loud: if one of the eight appears in the full sweep it is a compile error, never
a wrong proof.  This replaces `eval_op`'s silent `| _, w, _ => mk_bv w 0`
fallback (`LGraphModel.lean:200` and `:256`).

### 3. The general `Op_Sext` gap is closed

`pass_lean.cpp` emits `bv_sext`, which extends from the operand's own width and
ignores the amount, so only `amt = wa` and `amt = w ∧ w ≤ wa` are covered and any
other amount makes the design fail to typecheck.  `rsextV` handles **every**
`amt`, spelled as *"truncate to `n` bits, then read as signed"*.  Verified
computationally: `Sext(0b1000, 4) = −8`, `Sext(0b1000, 5) = +8` — so `amt` is a
WIDTH with the sign bit at `amt−1`, confirming that the official doc's
*"sign-extend from bit position b"* misleads.

### 4. No hand-written prefix induction was needed

The plan sketched `compileGraph_prefix_correct` as a fresh induction over the
node array.  That induction already exists: `GraphRefine.evalGraphG_of_localAgree`
(*uniqueness of the topo fixpoint*), proved once at `[NodeSemantics V]`.  Taking
φ = "read slot `k` out of the compiled environment" and discharging the local
recurrence with `compileOp_correct` plus two `Array.push` stability lemmas is
enough.  This is B2 paying for itself exactly as designed.

## Target-semantics independence — stated precisely

`denoteExpr` contains no `NodeSemantics.interpOp`, no `eval_op` and no
`GraphCert` traversal.  Mechanically checked: every occurrence of those names in
`ResidualIR.lean` / `ResidualSemantics.lean` / `Runtime.lean` is inside a comment.

Per operator the bridge obligation is one of:

* **real content** — `rsum` (`foldl` over `bv_uint` after take/drop vs
  `List.sum`), `rredOr`/`req` (explicit recursion vs `List.any`/`List.all`),
  `rmuxN` (`args[idx]?` vs an `idx < length` guard), `rsext`,
  `rmemRead`/`rmemWrite`/`rmemWriteBE` (enable test *inside* the lambda vs
  outside, so each write bridge is a genuine `funext` plus case split);
* **mapping only** — `rmux`, `rult`/`rugt`/`rslt`/`rsgt`, `rsra`, `rgetMask`,
  `rand`, `rorBits`, `rxor`, `rshl`.  The lemma checks the operand mapping and
  nothing else.

The mux case is the one worth naming: deps are `[sel, falseVal, trueVal]`, and
if `compileOp` swapped the last two, `rmuxV_correct` would not be provable.

Two honest reductions:

* A bit-level redefinition of the fold operators (And/Or/Xor/SHL) would validate
  more, at substantially more proof cost, against a bug class that has not
  occurred in the thirteen logged bugs. **Not done.**
* `rsext` is *general* but spelled with the same arithmetic as the source; the
  strictly stronger bit-level form `Y[i] = a[min i (n−1)]` is **not done**.
* An earlier draft also spelled the flop rule's branches `match` vs `if`. That
  difference validates nothing, so it was dropped; `xor` vs `if` for polarity was
  kept, because it does.

## Cost — all three DINO designs PROVEN

| design | sources | nodes | flops | wall | peak RSS | verdict |
|---|---:|---:|---:|---:|---:|---|
| `SingleCycleCPU` | 4,438 | 4,772 | 33 | 38.5 s | 7.45 GB | PROVEN |
| `PipelinedCPU` | 4,711 | 5,061 | 64 | 41.1 s | 7.50 GB | PROVEN |
| `PipelinedDualIssueCPU` | 9,862 | 10,740 | 97 | 150.0 s | 8.39 GB | PROVEN |

exit 0 and a clean axiom audit on all three; no `sorry` in any generated file.

Against the manual step-5 path on the same `SingleCycleCPU` (identical 4,772
nodes):

| | wall | peak RSS | emitted theorems |
|---|---:|---:|---:|
| manual step-5 path (bridge-enabled) | 22.7 min | 11.7 GB | **9,210** |
| this branch | **38.5 s** | **7.45 GB** | **0** |

~35× on wall clock, and the correctness comes from ONE design-independent
theorem rather than 9,210 generated ones.

Note how flat the first two rows are — 4,772 nodes → 38.5 s and 5,061 nodes →
41.1 s, both at ~7.5 GB. That is the shape "prove once" should have: the cost is
dominated by `native_decide` compiling the `DesignCert` literal, not by running
the compiler. `PipelinedDualIssueCPU` at 2.1× the nodes costs 3.6× the wall, so
there is some superlinearity in the literal compilation still to characterise.

## Validated

`formal/lean/probes/compiler_smoke.lean` — eight tiny certificates, every value
checked by `#eval`:

* add (wraps mod 2^8), mux (both arms, correct polarity), SRA (`−8 >>> 2 = −2`),
  Get_mask (mask `0b1010` over `0b1010` → **3**, packed to the low end, not 10),
  general Sext (both amounts);
* flop: active-low reset with reset value `0x5A`, enable, hold, and **reset
  priority over enable**;
* memory: byte-enabled write with a forwarded read — `be=1` gives `5` (new low
  nibble `0x5` from `0xA5`, old high nibble `0x0` from `0x0F`), `be=0` gives `0`;
* four refusals: forward dep, unsupported op, zero width, wrong arity.

End to end from RTL:

* `simple_add.v` → `DesignCert` → verified model computes `3+4=7`, `9+8=1`,
  `15+1=0`;
* `ram1.sv` → untouched read `15`, committed write `165`, read-back next cycle
  `165`;
* all three DINO designs export clean (`SingleCycleCPU` 4,772 nodes —
  **identical to the manual path's 4,772 `fv` defs**; `PipelinedCPU` 5,061;
  `PipelinedDualIssueCPU` 10,740).

## The `.ok` witness

`native_decide`, not the plan's `.get!`. `.get!` panics at *runtime* and leaves
the hypothesis undischarged; `native_decide` makes a compile failure a **build**
failure. `decide` is not usable — `Array.map` is not kernel-reducible (the kernel
gets stuck in `Array.instDecidableEqImpl`). This is the same axiom the manual
path's structural gates already use, so it adds no trust relative to that path.

Routed through `compileDesign_ok_witness`, so the evaluated proposition is a
single boolean `compilesOk` constructor test rather than a field-by-field
`DecidableEq` comparison of two whole `ResidualProgram`s.

### The generated-file interface, and the trap it avoids

**No `ResidualProgram` may appear in a theorem statement.** The obvious shape

```lean
def <Top>_residual := match compileDesign <Top>_designCert with | .ok R => R | ...
theorem <Top>_compiles : compileDesign <Top>_designCert = .ok <Top>_residual := ...
```

makes the kernel decide `.ok <Top>_residual` defeq `.ok (match compileDesign D …)`.
The kernel does not stop at a delta step — it *reduces* `compileDesign D`, and
`Array.push` is `⟨as.toList ++ [a]⟩`, so building 4,772 bindings costs O(N²) list
cells **as kernel terms**.

Measured on `SingleCycleCPU` (4,438 sources, 4,772 nodes):

| what the file contains | wall | peak RSS |
|---|---:|---:|
| the `DesignCert` literal + a trivial `native_decide` | 37.8 s | 7.4 GB |
| ↑ plus `compilesOk` evaluated by `native_decide` | 38.6 s | 7.5 GB |
| ↑ plus `#eval` of the compiled binding count (= 4,772) | 37.5 s | 7.4 GB |
| ↑ plus a theorem **naming** the residual | **OOM at 27 min** | **27 GB (40 GB cap)** |

The last row was first seen uncapped, where it reached **120 GB at one hour** on a
shared NFS server before being killed; re-run under `ulimit -v 40000000` it
panics with `INTERNAL PANIC: out of memory` after 1637 s. Both runs are the same
defect.

So the compiler *evaluation* is nearly free — the 7.4 GB is almost entirely the
cost of compiling the thousands-element literal, which `native_decide` pays for
any predicate at all. The blowup is purely the kernel defeq check.

The fix is `compileAndRun`, which keeps the `ResidualProgram` inside a function
body:

```lean
def <Top>_step : RuntimeInput → RuntimeState → RuntimeResult :=
  compileAndRun <Top>_designCert
theorem <Top>_compiles : compilesOk <Top>_designCert = true := by native_decide
theorem <Top>_step_correct : ∀ inp st,
    <Top>_step inp st = interpretDesign <Top>_designCert inp st :=
  compileAndRun_correct <Top>_designCert <Top>_compiles
```

`compileAndRun_correct` proves the general fact once, by `cases hc : compileDesign D`
— which *generalizes* the compiler's result instead of evaluating it. The
generated theorem is then one delta-unfold from it. A `<Top>_residual` def is
still emitted for `#eval` convenience, and deliberately named in no theorem.

### One diagnosis I got wrong first

The first DINO run failed on all three designs in ~25 s with 7.4 GB RSS and
`sorryAx`. I read the RSS and guessed `native_decide` was not surviving at scale,
and wrote the cheaper witness above as the fix. The actual error was

```
line 10: maximum recursion depth has been reached
```

— the default recursion depth is exhausted while **elaborating** the one
thousands-element array literal, which makes `<Top>_designCert` noncomputable,
which cascades into every declaration below it (including `native_decide`, whose
failure then looks like a proof failure). The legacy emitter has emitted
`set_option maxRecDepth 1000000` + `maxHeartbeats 0` all along for exactly this
reason; my exporter simply did not. Adding those two lines is the fix.

The cheaper witness is a genuine improvement and was kept, but it was not the
bug. Nor, as the table above shows, was `native_decide` scale — my second guess.
The third attempt found it.

## Exporter

`formal.lean.mode=verified_compiler` emits **only** `<Top>_designCert`, the
residual program (derived by the *verified* compiler, not re-emitted from C++),
the witness, and the instantiated theorem. No `<Top>_comb`, no `<Top>_next`, no
`<Top>_step` model, no per-node proof scripts.

It reuses the existing topo walk, the existing `cert_node_expr` operator
spellings and the existing memory decomposition; all it adds is the remap onto
the dense slot space and the formatting.

`initial` (reset value) and `negreset` (active-low) become real `FlopDesc` fields
in this mode instead of a `fatal()`, because the Lean side can now model them.
The legacy path still refuses them, since it hardcodes reset value 0 and
active-high polarity.

## Not done

1. **The full sweep** — 78 CVA6 + 122 CORE-ET, bucketed *proven* /
   *blocked upstream* / *blocked here*, into `pass/lean/SWEEP_b1-b2.tsv`.
2. **Differential comparison** against the legacy model (verification plan
   step 5).
3. **The proof-producing reifier** — fixed-width `BitVec` records and fast named
   defs, with `<Top>_fast_step = denoteResidual <Top>_residual`. A second trusted
   boundary until proved; deliberately deferred, not skipped.
4. **Sync-read memory** (`type == 1`) is deliberately rejected in verified
   compiler mode. Its registered read-data value still needs an explicit source
   and next-state descriptor before it can be accepted soundly.
5. **Complete accepted-certificate validation.** `compileDesign` checks node
   dependency order and operator shape, but not all output/flop/memory slot
   bounds or runtime source ordinals. The C++ exporter now fails on missing
   ordinals; the generic Lean compiler should still add shell-reference checks.
6. **The CVA6 flop-pin sweep re-run** — the earlier run died on
   `unknown package 'ariane_pkg'`, a filelist bug, so that result is inconclusive.

## The residual trust boundary

```
LiveHD LGraph → DesignCert
```

needs exporter validation or an independent checker. Everything after
`DesignCert` is covered generically.

And the standing caveat: this proves the compiler matches **`eval_op`**, not that
`eval_op` matches LGraph. Grounding measured three operators against the official
doc (`GROUNDING_SRA_GETMASK_SEXT.md`) and found **no implementation divergence** —
but the doc misleads on `Sext` and is silent on `SRA` widening and `Get_mask`
packing, so it cannot be transcribed mechanically.
