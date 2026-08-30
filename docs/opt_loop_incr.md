# The incremental compile loop

**What it is.** `lhd` can reuse the result of a previous compile when the
sources have not meaningfully changed. This document describes how that works,
what makes a cached result valid, and what is still wrong with it. The detailed
measurement record — the 2026-08-14 audit findings, the harness items, the lever
pool, the iteration log and the traps, all the F/H/L/I/T numbers other docs and
scripts cite — lives in [`opt_loop_incr_log.md`](opt_loop_incr_log.md). The live
numbers live in [`current_opt_loop_incr.html`](current_opt_loop_incr.html),
rendered from `../lhdsuite/bench/ledger.jsonl`.

Its sibling is [`opt_loop_synth.md`](opt_loop_synth.md): same shape, different
objective. That one optimizes the *netlist*; this one optimizes the *rebuild*.

> **No number in this document is a target.** Wall-clock figures are proxies
> from one machine and one build, kept for orientation and ratio only. Compare
> deltas within one host's ledger, never across hosts (ruling **I11**).

---

## 1. The objective

Make the edit → result cycle cheap:

```
lhd compile  →  pass.formal  →  pass color / pass abc  →  lhd sim  →  lhd lec
```

Two goals, in priority order:

1. **Cut WARM wall time** — what a rebuild costs when a `--workdir` is already
   populated.
2. **Do not pay for it** in cold wall time, in simulated cycles/s, in cache
   size, or in soundness.

The whole flow is in scope, but this document concentrates on `lhd compile`,
because every other flow reaches the front end through the same
`compile_sources`: `lhd sim` and the `lhd lec` prerequisite compiles inherit
whatever the compile tier does.

---

## 2. The idea in one page

A compile is a function of its inputs. Store the output beside a description of
the inputs; next time, if the description still matches, hand back the output.

Everything hard is in "still matches".

### The store

One scope per (design, option context) under the user's `--workdir`:

```
<workdir>/incr/scopes/compile/<top>/
    inventory.json          the source manifest: one row per source unit
    pyrope/unit_NNNN.prp    a byte copy of each source file
    ln/unit_NNNN/           its parsed LNAST, in a compact serialized form
    lg/                     the whole post-pipeline graph library
    lg/graph_inventory.json one row per graph: name, interface, body digest, owner
```

Two things about that layout are worth stating plainly.

**The `pyrope/` copy is the input, not a change detector.** A warm compile reads
the snapshot, never the user's file. So an edit landing mid-compile cannot be
half-consumed: the run either used the old bytes throughout or the new bytes
throughout. This is the same discipline a build sandbox uses, and it is why the
copy is not optional.

**The `lg/` tree is the *finished* design.** It is written after every pipeline
pass, `pass.formal` and `pass.legalize` included, so restoring it means running
none of them.

### Two tiers

| tier | grain | reuses | skips when it hits |
|---|---|---|---|
| **A** | one source file | its LNAST | the parser |
| **B** | the whole design | the graph library | upass, tolg, cprop, formal, legalize, save |

Tier A is what makes an edit cheap to *detect*. Tier B is what makes it cheap to
*skip*. Tier A hitting on 179 of 180 files is worth almost nothing on its own —
parsing is a small fraction of a compile. Tier B is where the time is.

### The algorithm

```
compile(seeds, workdir, options):

    scope   ← workdir/incr/scopes/compile/<top>
    context ← the resolved options that can change the output
    prior   ← read scope/inventory.json,  rejecting it unless
                  schema and code-salt match, and context matches

    # ---- capture: walk the import closure, deciding per file ----
    for each source unit u reachable from seeds:
        u.bytes ← read u
        if prior has u  and  u.bytes == snapshot(u):
            u.state   ← HIT             # skip the parser entirely
            u.lnast   ← the cached tree
            u.imports ← the cached import list
        else:
            u.imports ← scan_imports(u.bytes)        # lexical, cheap
            u.lnast   ← parse(u.bytes)
            u.clean   ← u.lnast is exactly equal to the cached tree
                        # a comment-only edit lands here: reparsed, still clean

    # ---- decide what is dirty ----
    dirty ← { u : not u.clean }
    dirty ← close(dirty)                # §4 — this is the interesting part

    # ---- reuse ----
    if dirty is empty and the only output is an lg: directory:
        materialize the stored library by file inventory;  return    # the fast path

    inventory ← read scope/lg/graph_inventory.json
    validate every stored graph against it (§3)
    restore the bodies owned by clean units
    replay their stored diagnostics
    run the pipeline over the dirty ones only

    store the new generation
```

**The digest proposes; an exact comparison decides.** A hash never authorizes
reuse on its own. Tier A hashes the LNAST to *find* a candidate and then walks
both trees node by node. Tier B keys on a digest and then compares every stored
graph's interface, body digest and derived owner against the inventory. A hash
collision can therefore cost time; it cannot cause a wrong build (ruling **I2**).

---

## 3. What makes a cached result valid

Reuse requires all of the following. Anything else is a cold compile.

```
1.  a user-named --workdir, and lhd.incremental is on
2.  inventory schema version matches
3.  code salt matches
        kCompileSrcSalt is a build-time hash of the front end and
        lowering sources.  Rebuilding lhd invalidates every cache.
4.  option context matches
        top, recipe, source paths, input directories, and --set flags
5.  per file: path unchanged AND (bytes identical OR parsed tree identical)
6.  the design's set of graphs matches the stored inventory, row by row:
        name, interface hash, has-body, body digest, and an owner that is
        RE-DERIVED here rather than trusted from the manifest
7.  no errors in the stored generation
```

Rule 3 is deliberate and expensive: correctness of a cached compile depends on
the compiler binary, so the binary is part of the key. Expect a cold first build
after every `bazel build`.

Rule 4 is currently **too coarse** — see §7.

---

## 4. What a change invalidates

This is the rule that decides whether incremental compile is useful or
decorative, and it changed on 2026-08-30.

### The rule

**Source units are compiled independently.** Elaborating file `B` does not
consume anything from file `A`'s body. Nothing propagates constants or values
across a module boundary at compile time — that only happens if a module is
*inlined*, which is not the default. So:

> A change to a unit's **body** dirties that unit alone.
> A change to a unit's **interface** additionally dirties every unit that
> instantiates or imports it.

Interface means everything a caller can observe without looking inside: port
names, widths and signedness; exported `pub` values and named types; generic and
template parameters; statefulness and the clock/reset interface; declared
stages.

```
interface_key(u) = hash(exported names, port shapes, pub values,
                        named types, generics, statefulness, stages)
body_key(u)      = hash(the parsed tree)

dirty(u)  ⟸  body_key(u)      changed
dirty(v)  ⟸  interface_key(u) changed, for every v importing u
```

### What the code does today

It dirties the **whole transitive reverse import cone** of any changed unit —
body change or not. On a design where everything imports a common package, one
leaf edit dirties the design.

Measured on a three-file `leaf → mid → top` chain: editing **top** restores two
of three graphs; editing **leaf** restores none. That is exactly backwards from
how people edit code.

Replacing the closure key with an interface key is the single largest correctness-
neutral speedup available, and ruling **I12** says it is not merely an
optimization: the current behavior is over-conservative with respect to the
language's actual semantics.

### The one exception — and it is a real one

`pass.legalize`'s **acyclic repair** (`flatten_false_loop_subs`) breaks a false
combinational loop by *inlining a pure-comb callee's body into its caller*, and
rewrites the caller in place. When that fires, the caller's stored graph
literally contains a copy of the callee's body.

So an interface-only rule would be **unsound** there: change the callee's body,
keep its interface, and the caller — judged clean — is served a cached graph with
the old callee baked in. That is a miscompile, not a slow build.

The fix is small and exact: when legalize dissolves an instance into a def,
record the dissolved callees on that def's inventory row, and add one clause:

```
dirty(v)  ⟸  body_key(u) changed, for every v that has u INLINED into it
```

The repair is a no-op on most designs — it only fires on the false-loop shape —
so the extra dirtying is rare. But the clause has to exist before the interface
rule can be trusted. See §5 for the rest of the legalize story.

---

## 5. What `pass.legalize` does to the picture

`pass.legalize` runs unconditionally at the end of every compile, after all the
optimization passes, and it is the last thing that may reshape a graph. Two of
its three jobs create or destroy graphs, so **one LNAST-generated def can become
several LGraph defs**:

1. **Acyclic repair** — dissolves a pure-comb `Sub` instance that sits on a false
   combinational loop, inlining it into the caller. Discussed in §4.
2. **Loop split** — splits a loop body def into two, `<body>__par` and
   `<body>__ind`, and drops the original when nothing instantiates it any more.
3. **Freeze** — records each def's structural digest so a later pass that
   reshapes it can be named.

Three consequences for the cache, in decreasing order of how much they should
worry you:

- **The inlining creates a cross-file *body* dependency.** §4. This is the one
  that can produce a wrong answer.

- **Split halves are attributed by name, and that works — by convention.** Graph
  names are `<unit>.<def>`, and a half is `<unit>.<def>__par`, so the
  dotted-prefix ownership rule (`unit u owns u and u.*`) still attributes it to
  the right source unit. The names are derived from the body's own name with no
  counters, so they are stable across runs and across unrelated edits. Nothing
  is broken here today, but the whole scheme rests on that naming convention: a
  half whose name lost the unit prefix would get an empty owner, which makes it
  unrestorable (harmless) *and* invisible to the manifest-scoped ghost pruning
  (not harmless — it would survive as a stale def in a shared `lg:` directory).

- **Legalize runs only over freshly-lowered graphs**, which is correct — restored
  graphs were stored already legalized. But two of its operations reason about
  "the design" while seeing only the fresh subset: `make_half` replaces a
  same-named stale half in the library, and the dead-body sweep counts
  instantiations across the graphs it was handed. Neither is reachable today
  (a restored graph already references its halves, so a fresh split cannot
  orphan a body a restored graph still uses), but both deserve a guard rather
  than an argument, since the argument is what an interface-only invalidation
  rule would change.

---

## 6. Diagnostics

A warm compile must print what a cold compile prints. That is a usability
requirement, and until 2026-08-30 it was implemented as a *reuse* requirement:
if the stored generation carried any warning and the run was not fully clean,
reuse was refused outright and the whole pipeline re-ran.

**Ruling I13 removes that.** A warning is not a correctness signal and must never
block reuse. Errors still do — a failed compile stores nothing.

The replacement follows directly from §4's independence rule: **attribute each
diagnostic to a source unit, and replay the clean units' records.**

```
attribute(record):
    if record.span names a file that maps to a unit → that unit
    elif the emitting pass was working on graph g   → owner_of(g)
    else                                            → unattributed

on a partial restore:
    replay  every stored record attributed to a CLEAN unit
    emit    every record the live pipeline produces for the dirty cone
    unattributed records are never replayed — the dirty run re-emits them
```

The span already carries a file today, so most of this is free. Deduplication
must key on `(code, span, message)`: dropping the span collapses distinct sites
into one and silently loses warnings.

One accepted imprecision: after a comment-only edit, a replayed record's byte
offsets are the *stored* generation's offsets and can be stale by the length of
the insertion. Re-resolving them would require the trees the restore exists to
avoid building. Locations are exempt from the warm-equals-cold standard for the
same reason (ruling **I2**: warm ≡ cold is *structural*).

---

## 7. Where the time actually goes

Measured 2026-08-30 on one host with one `lhd` build — proxies, not a baseline.
minion, 179 Pyrope units, `lhd compile … --emit verilog:`:

| scenario | wall | verdict |
|---|---:|---|
| cold | 4.71 s | — |
| no edit, `--emit-dir lg:` only | 0.055 s | 85× — the fast path |
| no edit, `--emit verilog:` | 0.68 s | 12× slower than the same hit above |
| comment-only edit | 0.88 s | works |
| one-token semantic edit to one leaf | 4.53 s | **1.04× — the cache buys nothing** |
| any unrelated `--set pass.abc.*` | 4.64 s | full cold, every time |

Five things that reads out of:

1. **A real edit is not incremental at all.** Refused for the warning reason
   (§6), then dirtied across the whole reverse cone (§4). Fixing those two is
   the project.

2. **A hit costs 12× more than it should unless the output is a bare `lg:`
   directory.** Two causes, both mechanical. The Tier-B restore re-digests
   *every* body in the cached library to validate it (341 ms on minion), while
   the `lg:` fast path validates a published file inventory instead (15 ms).
   And the driver materializes the entire LNAST forest *before* attempting the
   Tier-B restore, which a total hit never reads — roughly 200 ms of pure waste,
   in violation of the comment on the function that does it.

3. **The option context is too coarse.** Every `--set` goes into the key,
   including pure synthesis and STA knobs that cannot reach the front end.
   Adding `--set pass.abc.area_relax=200` throws the whole compile cache away.
   The key should cover the options that actually feed the front end and
   lowering — the same closure the code salt already hashes.

4. **`pass.formal` is 44% of a cold compile** (2.08 s of 4.71 s) and has no
   cache. Irrelevant to a hit, paid on every miss — which today means every real
   edit.

5. **The cache is large and nothing collects it.** 67 MB for 3.9 MB of source,
   and cold wall time rises 24–31% on big designs to populate it.

### A warning about the scoreboard

The `Compile only` row for **minion** on `current_opt_loop_incr.html` reports
0.91× for the *no-change* scenario — worse than not caching. That number is
real but it is not measuring what its column header says. `minion/tests/comment1/`
and `dino/tests/comment1/` are checked-in variant files that went **stale** when
the Pyrope corpus was regenerated on 2026-08-20: they now differ from
`pyrope/` semantically, not just by a comment. Reproduced — a true comment-only
touch on the same design is 0.88 s, the shipped fixture is 7.05 s.

The designs whose variants are *generated* by appending a comment line
(`matched_filter`, `xs_alu`, `xs_renametable`, `cva6`) report 15×–32× on the
same column. Re-derive the two checked-in fixtures, or convert those cores to
generated variants, before reading anything else into those rows. A benchmark
fixture that drifts silently is worse than no benchmark.

---

## 8. The queue

Ranked by measured time, most valuable first.

1. **Per-unit diagnostic attribution, and stop refusing on warnings** (§6).
   Unblocks a whole mode rather than a percentage: today every design that emits
   one warning recompiles cold on every semantic edit.

2. **Interface-keyed invalidation, with the inlining clause** (§4, §5). Turns
   leaf edits — the common case — from whole-design rebuilds into local ones.
   Ships together with the legalize `inlined:` record; neither is safe alone.

3. **Make a hit cheap** (§7.2). Validate the graph library the way the `lg:`
   fast path does, and try the Tier-B restore before materializing the LNAST
   forest.

4. **Narrow the option context** (§7.3). Small, mechanical, and it stops
   unrelated flags from destroying the cache.

5. **Cache a `pass.formal` verdict** (§7.4). A `cold_ms` lever, and the residual
   cost on the dirty cone once 1 and 2 land.

6. **Cache size and collection** (§7.5).

7. **Sub-file grain**, only if XiangShan-scale is the target. `Rob.prp` is
   405,279 lines in a single `pub mod`, so per-file grain gives it nothing: a
   one-character edit costs a 20 s reparse. Needs per-def or intra-file
   incremental parsing. Do not start here.

---

## 9. Rulings

Stable identifiers — other documents, tests and `../lhdsuite/bench` scripts cite
these by number.

| # | ruling |
|---|---|
| **I1** | Warm wall time is the primary number; cold wall time is the guardrail. Halving the warm rebuild for 5% more cold time is a win; halving it by doubling cold is not. Measure both, always. |
| **I2** | Soundness is not tradeable. A wrong reuse is a miscompile, which is strictly worse than a slow build. Warm must equal cold *structurally*; source locations are exempt, because a comment insertion shifts every byte offset. A digest collision must be harmless by construction — the fingerprint proposes, an exact comparison over preserved inputs decides. |
| **I3** | Simulated throughput is a hard guardrail. Cheap ways to cut simulator *build* time — splitting translation units, lowering the optimization level, coarsening inlining — cost cycles/s. A change that buys compile time with simulated throughput is rejected even if net wall time improves. |
| **I4** | Hit *count* is never the gate; time *re-done* is. minion once hit 199 of 264 ABC regions for a 1.0× speedup, because everything expensive was in the 65 that missed. |
| **I5** | A `store-failed` is a bug; a principled refusal is reported, not hidden. "This shape is not reusable" is a design decision and must be counted separately from "the cache tried to snapshot this and could not", which recomputes forever. |
| **I6** | Every item is a **lever**, drawn one at a time and measured — never a prerequisite phase to be completed first. Ordering constraints live on the individual levers, not on a calendar. The one exception is the measurement harness: nothing is meaningful until the numbers are trustworthy. |
| **I7** | One lever per iteration; the ledger is the product. A two-change iteration cannot be attributed. A measured negative result is recorded and is worth as much as a positive one. |
| **I8** | No per-design tuning. Every lever is a shared default. The only action a warm build may require of a user is naming a `--workdir`. |
| **I10** | One ledger, shared with `opt_loop_synth`; rows carry a `flow` field. The ABC **key and salt soundness** rules are owned here and consumed there; the ABC **recipe and partition knobs** are owned there and never touched here. |
| **I11** | Numbers are per-host and per-baseline; nothing here is a target. Stamp `host`, `lhd_git_sha`, `lhdsuite_git_sha` and `pdk_version` on every row and compare only within one host. Re-baseline after any PDK, toolchain or machine change. |
| **I12** | *(2026-08-30)* **Source units compile independently.** Elaboration does not propagate constants or values across a module boundary — only inlining does, and inlining is not on by default. A **body** change dirties its own unit; only an **interface** change dirties the units that import it. The current transitive-import-closure invalidation is over-conservative with respect to the language, not merely unoptimized. Its one true exception is legalize's inlining repair (§5), which must be recorded explicitly. |
| **I13** | *(2026-08-30)* **A warning never blocks reuse.** Errors do — a failed compile stores nothing — but a warning is not a correctness signal. Warm-equals-cold in the diagnostic stream is achieved by attributing records to source units and replaying the clean ones (§6), never by refusing to reuse. |

*(**I9** selects which designs are routine benchmark targets and which are
periodic stress runs; it is recorded with the rest of the target selection in
[`opt_loop_incr_log.md`](opt_loop_incr_log.md).)*

---

## 10. Traps

- **A benchmark fixture can drift.** §7. A checked-in "comment-only" variant
  stops being comment-only the moment the corpus is regenerated, and nothing
  fails — the scenario just quietly measures something else.
- **`./bazel-bin` points at the last-built configuration.** Two sessions
  building `-c dbg` and `-c opt` poison each other's timings.
- **Source order is load-bearing.** LNAST order feeds uPass's registries and CSE
  representative choice. Restoring graphs in map order rather than captured
  order produced a structurally different — though semantically equal — design.
  Any partial restore must rebuild the forest in the original captured order.
- **Store ordering is load-bearing.** Publish the parsed tree *before* its source
  snapshot. The reverse tearing pairs new bytes with an old tree, which
  byte-matches the user's edit and validates against the old manifest: a
  permanent silent false hit.
- **Partial Tier-A loading is closed.** Loading only the dirty roots was tried
  and reverted: it lost package `pub` namespaces and produced a structurally
  different top. It stays closed until the source-level dependencies actually
  consumed during elaboration are recorded explicitly — which is what §4's
  interface key is for.
- **Ownership must be derived, never trusted.** The manifest says which unit owns
  a graph; the restore recomputes it. Trusting the stored value would let a
  dirty graph masquerade as belonging to a clean unit.
- **Ghost pruning must be manifest-scoped.** A shared `lg:` output directory
  legitimately accumulates definitions from other compiles. Only definitions
  *this* compile's units used to own may be deleted.

---

## 11. Where things live

| | |
|---|---|
| implementation | `lhd/lhd_compile_cache.cpp`, driven from `lhd/lhd_kernel_compile.cpp` (`compile_sources`) |
| acceptance tests | `lhd/tests/lhd_compile_cache_test.sh` — gating and telemetry, exact comment-only reuse, semantic invalidation (including a manufactured digest collision that the exact tree compare must still reject), context mismatch, cache damage as a refused cold miss, `store_failed` as a hard failure, structural warm≡cold over a mixed dirty cone, ghost-definition pruning, shared-workdir coexistence, diagnostic replay |
| benchmark harness | `../lhdsuite/bench/matrix.sh` — one row per (phase, mode) into `bench/ledger.jsonl` |
| live scoreboard | [`current_opt_loop_incr.html`](current_opt_loop_incr.html), rendered from the ledger |
| detailed record | [`opt_loop_incr_log.md`](opt_loop_incr_log.md) |
| sibling loop | [`opt_loop_synth.md`](opt_loop_synth.md) |
