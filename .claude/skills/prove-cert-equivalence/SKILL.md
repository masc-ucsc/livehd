---
name: prove-cert-equivalence
description: >
  Prove (in Lean 4) that a generated fast BitVec model equals its graph
  certificate — the pass.lean "step-5 fast-view bridge"
  (<Top>_comb/_next/_step = _cert) — and make the proof scale to large circuits.
  Use when: closing the step-5 bridge for a new design (DINO, CVA6, ...),
  debugging a bridge that won't typecheck, or a bridge check that is too slow /
  runs out of memory. Covers the O(N) representation, how to find the (usually
  const-spelling) bugs, how to estimate runtime, and the good-citizen run recipe.
allowed-tools: Bash, Read, Grep, Glob, Edit, Write
---

# Proving certificate equivalence (pass.lean step-5 fast-view bridge)

Goal: for a design `<Top>`, prove the emitter's fast executable `BitVec` model
equals its graph certificate — `<Top>_comb i s = <Top>_comb_cert i s` (and
`_next`/`_step` for sequential designs). Combined with the LEC gate (fast ≡ RTL),
this closes RTL ⇔ certificate.

Repo: `livehd-new`. Emitter: `pass/lean/pass_lean.cpp`. Lean support:
`formal/lean/LeanSemanticPrimitives/Translation/{GraphRefine,OpBridge,LGraphModel}.lean`.
Knob: `--set formal.lean.emit_fast_bridge=true` (default off). Op census gate:
`pass/lean/scripts/op_census.py`. Bug log: `pass/lean/STEP5_BRIDGE_BUGS.md`.

## 1. Architecture (three pieces)

- **Piece B — general theorem (design-independent, already proven).**
  `GraphRefine.evalGraph_of_localAgree`: any environment `φ` that (a) agrees with
  the source env off-topo and (b) satisfies the per-node recurrence
  `φ n = evalNode G φ n` for every topo node, equals `evalGraph` on all topo
  nodes. `GraphCert.nodes : Nat → Option NodeCert` is a **function** and the
  theorem uses it abstractly — so the graph-lookup representation is a free
  choice (this is what makes §2 possible).
- **Piece A — per-op bridge lemmas (`OpBridge.lean`).** Relate the cert evaluator
  `eval_op OP w [bvenc a, …]` to the native `BitVec` op, under
  `bvenc x = mk_bv w (Int.ofNat x.toNat)`. One lemma per op
  (`and_bridge`, `sum2_bridge`, `getmask_bridge'`, `sext_bridge`,
  `sext_bridge_low`, `muxn3_bridge`, `eq_bridge`, …), plus normalization lemmas
  (`bv_zext_id`, `bv_to_bitvec_bvenc(_zext)`, `ofInt_zero_eq`, `mk_bv_ofInt`).
- **Piece C — the emitter (`pass_lean.cpp`).** Prints, per design: `φ`
  (`<Top>_phi`), per-source `sourceEnv id = bvenc leaf` facts, well-formedness
  (`nodup`/`some`/`depord`, by `native_decide`), one standalone recurrence theorem
  per node (`<Top>_rec<id>`, discharged by that node's op bridge), a combiner
  (`<Top>_bridge_rec`), `<Top>_bridge_src`, and `<Top>_comb/next/step_refines_fast`.

Per-node proof shape (uniform): `show bvenc (fv_id) = eval_op OP w [deps…]; rw
[srcfacts, <op bridge>]; simp only [fv_id, <closer>]`. Deps are resolved by
*defeq* in the `show` (topo dep → `bvenc (fv_dep)`, source → `sourceEnv dep`) —
never `simp [phi]` (that materializes all of `φ`).

## 2. The scaling law — the single most important thing

The per-node proof kernel-reduces the shared `φ` and `graphCert.nodes` lookups.
**How those are represented decides the whole complexity:**

| Representation of `φ` / `graphCert.nodes` / `sourceEnv` | Per-node lookup | Total |
|---|---|---|
| Flat `if n = k₁ then … else …` chain / `List.find?` (monolithic) | O(N) scan | **O(N²)** |
| Balanced binary-search `if`-tree | still O(N) — **beta-substitutes the key across the whole N-node term** | **O(N²)** |
| **Inductive tree DATA + recursive `find`, values as input-independent closures** | O(log N) | **O(N)** |

Measured (self-contained synthetic, width-8 chain): monolithic ≈ N^1.8; if-tree
≈ N^1.9; **data-tree ≈ N^1.0** (135→261→662 s at N=1000→2000→4772). Monolithic
DINO (4772 nodes) never finished after ~47 h / 94 GB; data-tree completes with
**bounded ~11 GB**.

**The data-tree pattern (this is the fix — put it in the emitter, or transform an
existing generated file):**
```lean
inductive BT (α : Type) | lf | nd (key : Nat) (val : α) (lo hi : BT α)
def BT.find : BT α → Nat → Option α
  | .lf, _ => none
  | .nd k v lo hi, n => if n < k then BT.find lo n else if n = k then some v else BT.find hi n
def BT.keys : BT α → List Nat | .lf => [] | .nd k _ lo hi => k :: (BT.keys lo ++ BT.keys hi)
theorem BT.find_eq_none (t) (d) (h : d ∉ t.keys) : BT.find t d = none  -- induction; no sortedness needed

-- values are i/s-independent CLOSURES so building the tree never substitutes the input into N slots:
def <Top>_phiTree : BT (In → State → BV) := <balanced BST literal of (id ↦ fun i s => bvenc (fv_id i s))>
def <Top>_phi (i) (s) : Nat → BV := fun n => (BT.find <Top>_phiTree n).elim (<Top>_sourceEnv i s n) (fun f => f i s)
```
Why it works: `find` navigates the tree **value**, comparing the key locally at
each visited node (O(log N)); a nested-`if` term instead beta-substitutes the key
into all N occurrences (O(N)). Closures keep the tree input-independent so
building it never substitutes `i`/`s` into N positions either.
`bridge_src` uses `BT.find_eq_none` + `BT.keys phiTree ⊆ topo` (see §5 caveat).

For an existing generated (monolithic) file, a Python transformer that rewrites
**only** the three defs `φ`, `sourceEnv`, `graphCert.nodes` to data-tree form and
copies every per-node theorem verbatim is the fast way to get the O(N) version
(the emitter change is the production path).

### 2b. The SECOND quadratic: the combiner (bites after you fix the lookups)

Fixing the lookups is not enough.  The combiner
(`<Top>_bridge_rec : ∀ n ∈ topo, φ n = evalNode G φ n`, which glues the N per-node
theorems together) is the other O(N²).  **Never emit it as a case split:**
```lean
-- BAD: one theorem, 4778 lines, rcases pattern 19115 chars
  intro n hn
  simp only [G, List.mem_cons, ...] at hn   -- membership -> N-way disjunction
  rcases hn with h | h | … (N alternatives) -- N-deep Or.casesOn; each level's
  · exact <Top>_rec_a i s                   --   motive carries the REST -> O(N²)
  …                                          -- plus `subst` re-checks the goal N times
```
Emit a **term-mode right fold** instead — no case analysis, no goal substitution,
and each level's tail is a *shared* suffix subterm:
```lean
theorem <Top>_bridge_rec … : ∀ n ∈ G.topo, … :=
  List.forall_mem_cons.mpr ⟨<Top>_rec_a i s,
  List.forall_mem_cons.mpr ⟨<Top>_rec_b i s, … List.forall_mem_nil _⟩⟩
```
Measured on DINO (4772 nodes, ≤8 cores, same file otherwise):

| section | wall |
|---|---|
| head: 4772 `fv` defs + 3 BT trees + 4438 src-facts + wf | **181 s** |
| + 200 rec theorems | 171 s (marginal ≈ 0) |
| full file, **`rcases` combiner** | **> 3 h 56 m, never finished** |
| full file, **term-fold combiner** | **1391 s = 23.2 min, 13.3 GB, exit 0, 0 errors** |

The combiner is one declaration, so it is inherently **single-threaded** — the
telltale signature is one thread with hours of CPU while the rest sleep (§3).
A further lever (untested at scale, worth it for CVA6): **chunk** the fold into
K lemmas of ~64 nodes each and combine with `List.forall_mem_append`, which caps
any single term *and* restores parallelism across declarations.

## 3. How to find the bugs

**"0 sorries" ≠ "typechecks".** The emitter can produce a sorry-free bridge that
still has proof holes. They stay hidden while the check is O(N²) (never finishes)
— the O(N) representation is the first path fast enough to *reach and report*
them. So: **make it O(N) first, then the bugs appear.**

- **Errors surface incrementally.** `lake env lean` elaborates top-to-bottom and
  prints each `error: unsolved goals` / failed `decide` when it reaches that
  declaration, then continues. So **`grep "error:" <log>` at any time** — never
  wait for completion. Slowness ≠ bug (a bug is an `error:` line).
- **BUT the log lies about progress — two traps that cost me hours:**
  1. **stdio block-buffers at 4096 bytes when stdout is a file.** An empty log
     means "<4 KB pending", NOT "no progress". (Tell: a stalled log of *exactly*
     4096 bytes.) Use `stdbuf -oL` if you want live output.
  2. **Lean emits messages in file order**, so one slow *early* declaration
     withholds every later message. Never infer "it hasn't reached line X".
- **To locate a slow declaration, bisect — do not guess.** Build cumulative
  slices of the generated file (head → +200 rec → +Sext → +wf/keys → +combiner),
  time each against the head baseline, and see which one jumps. I guessed the
  culprit wrong **three times** (BST trees, Sext `first`-dispatch,
  `native_decide` on `keys_sub` — all measured ≈ baseline) before bisection
  pinned the combiner.
- **Thread accounting separates "stuck" from "starved":**
  `for t in /proc/<pid>/task/*; do awk '{print $3, ($14+$15)/100}' $t/stat; done`
  One thread with hours of CPU while 60+ sleep ⇒ a **single pathological
  declaration** (Lean parallelizes across theorem bodies, so a drained run means
  everything else finished). Low CPU with high system load ⇒ contention instead.
- **Almost every step-5 bug is a fast-vs-cert constant-spelling gap.** The fast
  const emitter (`lit_const_at`) and the cert leaf builder (`int_of_const` +
  `source_leaf`) must produce the *same* spelling. Known drifts: zero (`0#w` vs
  `BitVec.ofInt w 0`), all-ones mask (`(1#w <<< w) - 1#w` vs `BitVec.ofInt w
  (2^w-1)`), and `-1`/i64. For ops that erase the const via `bv_zext`
  (And/Or/arith) the closer's `bv_zext_id` absorbs it; for ops where the const
  **survives into the result** (EQ/ULT/UGT/SLT compares, MuxN branches) the
  mismatch surfaces as `unsolved goals`. **Systematic fix: make `lit_const_at`
  spell every constant as `BitVec.ofInt w <int_of_const>` (identical to the cert
  leaf).** It is value-identical, so LEC is unaffected.
- **A too-strict op-bridge side condition.** `sext_bridge` assumed
  `amount = operand_width`; DINO also has sign-*truncate* Sext
  (`amount = out_width ≤ operand_width`), needing `sext_bridge_low` and a
  proof-time `first | sext_bridge | sext_bridge_low` dispatch.
- **Source-driven outputs / flop dins (easy to miss — it hides in the tail).**
  An output (or a flop din) can be driven **directly by a source** — a constant,
  input, or flop — instead of by a computed node.  Then its cert id is a source id
  (≥ 1e9 for synthesized consts), there is no `<Top>_fv<id>`, and `hb <id>` fails
  because the id is not in `topo`.  Emit the off-topo path instead:
  ```lean
  rw [GraphRefine.evalGraph_not_mem G (<Top>_sourceEnv A) G.topo <id> (by decide),
      <Top>_src<id> A]
  ```
  (`evalGraph` only updates ids in the topo list, so an off-topo id reads through
  to the source env — `GraphRefine.evalGraph_not_mem`.)  Guard **both** the output
  loop and the flop-din loop on `topo_set.count(id)`.
- **Fix workflow (per bug):** read the exact `error:` goal → trace to the node id
  + op → characterize the root cause → **validate the fix against the exact
  failing pattern in a tiny isolated file** (seconds) → only then change the
  emitter/OpBridge and regenerate. Never debug by re-running the whole design.

## 4. How to estimate the runtime correctly

**Hard-won rule: synthetic sweeps give you the EXPONENT; only timed slices of the
REAL file give you the CONSTANT.** Estimating DINO from synthetic alone was wrong
by 10–50× (predicted 30–90 min, actual behaviour ranged from 26 min to
never-finishing depending on one declaration). Do this instead:

1. **Synthetic sweep** (`gen_*.py`, self-contained files of size N with the same
   proof kind) → log-log fit the **exponent** to compare *representations*
   (monolithic ≈ N^1.8, if-tree ≈ N^1.9, data-tree ≈ N^1.0). Use it to choose a
   design, never to predict a wall time.
2. **Timed cumulative slices of the real generated file** → the actual budget,
   and it simultaneously localizes any hot declaration (§3). Subtract the fixed
   Mathlib-import overhead (~11 s / ~6 GB) to get marginals.
3. **One full real run** for the number of record.

**Measured DINO reference (4772 nodes, ≤8 cores, data-tree + fold combiner):**

| what | value |
|---|---|
| full-file wall | **1391 s = 23.2 min** (8 cores, exit 0, 0 errors) |
| peak RSS | **13.3 GB** |
| head (defs+trees+src-facts+wf) | 181 s |
| marginal per rec theorem | ≈ 0 (200 rec ≈ free) |
| dominant term | the combiner |

Acceptance budget worth adopting: **< 30 min, < 16 GB at ≤8 cores**.

Parallelism note: Lean elaborates *theorem bodies* in parallel, so a wide file
starts multi-core and **drains** to one core as stragglers finish. A single huge
declaration therefore pins one thread — see §2b and the thread-accounting tell
in §3. Budget on ~1 core for whatever the largest single declaration is.

## 5. Gotchas / caveats

- **`bridge_src` via `native_decide`.** `BT.keys phiTree ⊆ topo := by
  native_decide` compiles the whole `phiTree` (all value closures) to native
  code; with heavy real closures this can be a long serial step. Prefer a
  keys-only structure or a `decide`/structural proof if it dominates.
- **`first | … | …` emits linter warnings** ("this tactic is never executed")
  for the losing branch — harmless; add `set_option linter.unreachableTactic
  false, linter.unusedTactic false` in bridge output to silence.
- **A silently-failed slice cut.** "take the first K theorems" by cutting at the
  next blank line silently grabs *all* of them if declarations aren't
  blank-separated — verify slice size (`grep -c '^theorem'`).

## 6. Resource discipline (shared NFS server — mandatory)

The dev box is an NFS **server**; heavy Lean starves it. Every run:
`LEAN_NUM_THREADS=8 taskset -c 0-7 nice -n 19 ionice -c 3 lake env lean <file>`
(≤ 8 cores). For long runs that must survive session/harness reaping, launch a
**detached transient service** (not `--scope`, which dies with the launcher) with
a cgroup cap and **no timeout**:
```
systemd-run --user -p CPUQuota=800% --unit=dino-full \
  nice -n 19 ionice -c 3 taskset -c 8-15 \
  bash -c 'export PATH=/mada/users/czeng14/.elan/bin:$PATH; cd .../formal/lean; \
           /usr/bin/time -f "%e s %M KB" lake env lean <file> > <log> 2>&1; echo exit=$? >> <log>'
```
Poll `<log>` / `systemctl --user status <unit>`. **Kill by explicit pid and
verify with `ps`** — never `pkill -f` a pattern that also matches the launcher
shell (it kills your own run and can leave orphans). `lean -o out.olean` (no
`-c`) builds an olean without native codegen (codegen was a 20 GB / 21 h red
herring — it is not what chunk imports need).

## 7. Verification ladder (cheapest first)

1. Each new op bridge, proven in isolation against its `eval_op` form, with an
   `example` of its emitter-facing use committed next to it in `OpBridge.lean`
   (so `lake build ..OpBridge` checks lemma and usage together).
2. **Static gates** (seconds): `pass/lean/scripts/op_census.py` (every `(op, arity)`
   in the design is handled by the dispatch) and
   `pass/lean/scripts/const_parity.py` (every const dep's cert leaf spelling
   appears verbatim in the consumer's fast body), plus `grep -c sorry` and
   `grep -c 'TODO(step5)'`.
3. A minimal 1-flop sequential design — validates `_next`/`_step`.
4. **A design with an output (and a flop din) driven DIRECTLY by a constant** —
   covers the off-topo/source path of §3. *This case is missing from the tiny
   combinational designs and the 1-flop design, which is exactly why that bug
   survived to a 26-minute run.*
5. A synthetic N-sweep — compares representations (exponent only, see §4).
6. The real design, run to completion under the cap.

### Testing discipline (learned the hard way)

- **A truncated probe is not a clean bill of health.** A slice built as
  `lines[:combiner] + new_combiner + "end"` silently drops `bridge_src` and all
  three `_refines_fast` theorems — it reported "exit 0, 0 errors" while omitting
  the ~1% of the file that held the bug. **Always state what a probe excludes**,
  and verify slice contents (`grep -c '^theorem'`), never trust line arithmetic.
- **Bugs cluster in the tail** (`bridge_src`, `_comb/_next/_step_refines_fast`),
  because it is the last thing elaborated and the first thing lost when a run is
  killed. To test the tail cheaply, **stub the combiner with `sorry`** — that
  skips its ~20 min and checks everything after it in a few minutes.
- **"0 sorries" + "static gates pass" ≠ typechecks.** Only a full-file exit 0
  counts. Track them as separate milestones.
- **Don't run several ~10 GB Lean instances at once** — they OOM each other and
  the failures look like results. Serialize probes, or cap total memory.

## 8. Quick command map
- Regenerate a design's bridge: `bazel build //lhd:lhd` then
  `./bazel-bin/lhd/lhd compile verilog <ref.sv> --reader yosys-verilog --top <Top>
  --workdir <w> --emit-dir lean:<out> --set yosys.setundef=zero
  --set formal.lean.strict=true --set formal.lean.emit_cert=true
  --set formal.lean.emit_fast_bridge=true`.
- Check for bugs mid-run: `grep 'error:' <log>`; count: `grep -c 'error:' <log>`.
- No Co-Authored-By in commits; `git commit -F <file>`.
