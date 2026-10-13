# CVA6: which modules, on whose authority, and from which revision

## The canonical 30 — found, and it is tracked, just not on this branch

The 30-block CVA6 set is real and has exact provenance. It is **not** in the D4
worktree, which is why an earlier reading of this branch alone concluded no such
set existed. It lives on `direction-3-translation-validation`:

| | |
|---|---|
| names | `pass/lean/manifests/cva6_30_frozen.tsv` (the freeze; names are READ, never recomputed) |
| current join | `pass/lean/manifests/cva6_30.tsv` (names + evidence, re-joined each run) |
| commit | `7f5b1bd27b2354c1a4a22268f0e3513b3fbc2954`, 2026-09-30, *"pass/lean: a gated D3 sweep harness, and target manifests that cannot drift"* |
| branch | `direction-3-translation-validation` (not an ancestor of D4 HEAD) |

**The selection rule, verbatim from the freeze file:**

> the 30 CVA6 core-hierarchy blocks (of the authoritative 78 in
> CVA6_COVERAGE_PLAN.md Phase 2) that had a certificate when the list was first
> frozen, ranked by node count, largest first. Ranking by size keeps the
> selection re-derivable and biases it toward the hard cases. Once frozen the
> list is READ, never recomputed.

So the 30 are a size-ranked subset of the authoritative 78. Verified here:
**all 30 are in `cva6_78.list`**, none is outside it.

**Integrity check, done rather than assumed.** `cva6_30.tsv` carries
`target30_sha256=8e99e348…b737cf`, defined in `d3_manifest.py` as an
order-independent SHA-256 over `(block, "<cert_available>:<cert sha256>")`
pairs. Recomputing it from the imported rows reproduces that digest exactly, so
the names and hashes copied into `cva6_30.list` are not garbled.

**The caveat that must travel with the number.** The freeze was taken on
2026-09-30 from `/HOMEROOT/projects/livehd-new/generated/cva6_vc/lean`, and
`pass/lean/manifests/PROVENANCE.tsv` labels that corpus
`IMPORTED: compatibility evidence only. Passing a gate here does NOT show this
branch can generate the certificate end to end.` The **names** are authoritative
and tracked; the **criterion** ("had a certificate at freeze time") is a property
of another checkout's corpus. Which of the 30 this worktree can actually build
is measured below, not inherited.

`pass/lean/tests/d4/cva6_30.list` holds the 30 names, sorted.

### Why the other candidate sets are not it

| count | artefact | what it is |
|---|---|---|
| 78 | `CVA6_COVERAGE_PLAN.md` Phase 2 | the authoritative TARGET hierarchy; the 30 are drawn from it |
| 44/46 | `generated/cva6_vc/prove.tsv`, cited at `DIRECTION4_INCREMENTAL.md:347` | an append-only historical sweep table in another checkout, with duplicate rows from a pre-v8 wrapper generation |
| 23 | `pass/lean/SWEEP_cva6.tsv` | a past sweep's results |
| 14 | `scripts/cva6_module_wrappers/` | hand-written wrappers |
| 3 | `pass/lean/tests/d4/pilot_cva6.list` | "a THREE-name pilot list, not a corpus" |

## The authoritative 78: two independent derivations, exact match

**Source of truth:** `/HOMEROOT/projects/cva6-blockfinding/cva6_blocks.txt`,
an inventory of openhwgroup/cva6 at commit **`6cb200105`** ("Fix Zcmt JVT index
calculation (#3451)", master). Section 16 TOTALS:

    CVA6 core hierarchy (core/, synthesizable, reachable from module cva6)     78

Derived twice and compared, symmetric difference empty:

1. the block-by-block enumeration in `CVA6_COVERAGE_PLAN.md` Phase 2 — nine
   headings whose declared counts (1, 8, 7, 7, 19, 4, 7, 24, 1) sum to 78, with
   exactly that many names in the bullet lists;
2. sections 1–9 of `cva6_blocks.txt` itself, re-extracted.

**The file's own caveat, which bounds every count downstream.** The 78 counts
mutually exclusive variants — three cache subsystems (std / wt / hpdcache), two
register files (ff / fpga), two BHTs. **Any single build elaborates fewer.** A
coverage figure must name its configuration or it is not a figure.

## The foundation, built here, from that revision only

`scripts/build_cva6_foundation.sh` produces the whole input set in this worktree
and writes `generated/cva6_foundation/provenance.txt`. It reads **nothing** from
`/HOMEROOT/projects/livehd-new`: not its frozen filelists, not its untracked
`temp/wrap_all`.

That is not hygiene for its own sake. Emitting `alu` from that stale wrapper
corpus gives 429 sources / 436 nodes; regenerated here it gives **6,137 /
6,597** — which is exactly what `cva6_30.tsv` records for `alu_gate`. A 5x
discrepancy that looks like a result is what pinning the inputs prevents.

    bender script flist-plus -t cv64a6_imafdc_sv39_hpdcache_wb     (353 lines)
      -> slang -f <filelist> --top cva6 --ast-json-detailed-types  (0 errors)
      -> gen_cva6_wrappers.py <ast> --out generated/cva6_wrappers

Pinned in the manifest by SHA-256: the module list, the CVA6 revision
(`6cb200105…`), the config package, bender and slang versions, the filelist, the
elaborated AST, the wrapper generator, the wrapper corpus (one hash over all 78
slots in declared order), the `lhd` binary, the Lean toolchain, and the D4 HEAD.

> Two wrapper hashes exist and they are NOT the same number.
> `generated/cva6_foundation/provenance.txt` has `wrappers_sha256`, over all 78
> slots. A sweep's provenance has `cva6_wrappers_declared_sha256`, over the
> modules that run declares. The names differ on purpose.

### What the pinned configuration elaborates: 52 of 78, 30 of 30

| 52 of 78 | wrapper generated |
| 26 of 78 | **not in this config** — `std_*` and `wt_*` cache (two of the three subsystems), `bht2lvl`, `ariane_regfile_fpga`, `acc_dispatcher`, `trigger_module`, `macro_decoder`, `zcmt_decoder`, the l15 adapter, and the axi adapters only those subsystems instantiate |
| 0 of 78 | generator failures |

Against the canonical 30: **30 generated, 0 blocked** (`cva6_30_vs_foundation.tsv`).

The 26 are the inventory's own alternates caveat, measured rather than assumed.

## Two fail-closed properties the builder now has

Both of these were MISSING, and a review probe exploited each.

### 1. The published corpus is wholly fresh

The builder wrote straight into a persistent wrapper directory and decided
`GENERATED` from *a file with that name exists* after the generator returned. A
planted `ghost_gate.sv` therefore survived a `SKIP ghost` and would have been
reported as generated output.

Generation now goes to a staging directory the previous run cannot reach, and a
module's status comes from THIS invocation's explicit `GEN`/`SKIP` line. Both
halves are checked against each other — exactly one result line per declared
module, a `GEN` must have staged a file, a `SKIP` must not, and nothing may be in
the staged corpus that was not declared and generated. The complete corpus is
published only after all of that holds.

### 2. Every CVA6 input is bound to one clean, named source state

Requiring `CVA6_ROOT` was not enough. A probe passed a **fake, non-git root
holding nothing but a copy of the config package** while keeping the real
checkout's filelist and wrappers: the sweep exited 0 and wrote `reconciled=YES`
with `cva6_root_rev=not-a-git-checkout`. Rows from a mixed root are not rows
about any source state.

The builder now **rejects tracked modifications** in `CVA6_ROOT` — a dirty tree
at revision R is not R — and records `cva6_tracked_clean=yes` plus the canonical
path. Untracked files are allowed, since the inventory `cva6_blocks.txt` is one
and is not a build input.

Before any block runs, the sweep verifies against ONE required foundation
manifest:

    CVA6_ROOT canonical path      CVA6 revision (and clean now)
    config package sha256          filelist sha256
    wrapper generator sha256       wrapper directory
    module list + its sha256       wrapper corpus sha256 (ALL declared modules)

The corpus hash covers every module the foundation declares, not this run's
subset, so a wrapper swapped anywhere is caught even when the run does not touch
it. Any mismatch is fatal before the first block.

## The one-element unpacked-array bridge

`csr_regfile.sdtrig_decoder_action_o` and `id_stage.sdtrig_decoder_action_i`
were the last two canonical-30 gaps. slang types them

    FixedSizeUnpackedArrayType  range [0:0]
      elementType PackedArrayType range [63:0] of logic

i.e. `logic [63:0] p [0:0]`. An unpacked array is not a vector, so the generator
declined. But a ONE-element unpacked array holds exactly one packed value, so the
gate boundary carries that element and a one-element internal array is wired to
the DUT:

    output logic [63:0] sdtrig_decoder_action_o,          // csr_regfile
      logic [63:0] sdtrig_decoder_action_o_u [0:0];
      assign sdtrig_decoder_action_o = sdtrig_decoder_action_o_u[0];
      .sdtrig_decoder_action_o(sdtrig_decoder_action_o_u)

    input  logic [63:0] sdtrig_decoder_action_i,          // id_stage
      logic [63:0] sdtrig_decoder_action_i_u [0:0];
      assign sdtrig_decoder_action_i_u[0] = sdtrig_decoder_action_i;
      .sdtrig_decoder_action_i(sdtrig_decoder_action_i_u)

No bits move, are reordered or are dropped: it is a bridge, not a
reinterpretation. It is deliberately narrow and must stay so — `[1:0]`, `[3:0]`,
`[1:1]` (one element but index 0 is out of range), an unknown element type, a
nested unpacked element and a plain packed array are all REFUSED, and the module
is skipped exactly as before. Flattening a multi-element unpacked array would
silently change the design; a skipped module is the better failure.

Both now emit and run: `csr_regfile` 32,853 sources / 34,897 nodes / 136 flops,
`id_stage` 10,503 / 11,204 / 30, each PARSED / `compiles_ok=true` / RAN at the
ordinary stack limit (`cva6_unpacked_bridge_rows.tsv`).
`cva6_unpacked_bridge_controls.sh` pins both directions and all six rejections.

## Generator defect found and fixed on this route

Three blocks (`instr_queue`, `scoreboard`, `perf_counters`) failed
`read_slang` with *use of undeclared identifier*:

    instr_queue_gate.sv:9   } fetch_entry_t__r;
    instr_queue_gate.sv:55  output fetch_entry_t [0:0] fetch_entry_o,

`gen_cva6_wrappers.py` reconstructs a type parameter under a local name
`<param>__r` and maps ports through `recon` so they use it — but only on the
SCALAR path. The packed-array path (`packed_array_of_named`) returned the
element's ORIGINAL name, so the port referred to an identifier the wrapper never
declares. `packed_array_of_named` now takes `recon` too. All three emit and run
afterwards. `cva6_foundation_controls.sh` check 3 asserts, over the whole
wrapper corpus, that no wrapper declares `<T>__r` and then declares a port as
`<T>`; a planted regression is detected.

## Trust statement

A runtime row and an artifact hash are **empirical translation validation**:
this certificate, emitted by this `lhd` binary from these sources, parsed and
ran and produced this digest. They are **not** a formal proof that the C++
LGraph-to-DCERT1 transcription is correct for all designs. The theorems
(`compileDesign_correct`, `runChecked_correct`, `parseCert_renderCert`) are
quantified over every `DesignCert`; what no theorem covers is whether the C++
built the right `DesignCert` from the RTL.
