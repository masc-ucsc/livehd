# Coverage blockers: owner, evidence, and what would clear each

Two things stop the CVA6 coverage count rising, and they have DIFFERENT OWNERS.
Neither is fixed here, and nothing on another branch has been touched.

Current count: **8 differential, 8 with no known reset conflict**, all on
canonical `cva6_30_auth` certificates, all zero-flop.  That is every supported
zero-flop block in the frozen 30.

---

## BLOCKER 1 -- systemic reset-polarity conflict in the exporter

**Owner: the DCERT1 exporter.** `pass/lean/pass_lean.cpp` and
`pass/lean/design_cert_export.hpp` in `livehd-d4-incremental` (the writer is
there; `livehd-new` has no `DCERT1` string at all).  NOT this branch, and not
something this branch should patch.

**What is wrong.** For every flop, a certificate records the reset polarity
TWICE -- once on the `flopQAsync` SOURCE (`resetInput`, `activeLow`) and once
on the `FlopDesc` ROW (`resetPin`, `resetActiveLow`).  In the authoritative
corpus these two disagree, always the same way: the source says
`activeLow = 1`, the flop row says `activeLow = 0`, on the same input pin.

**Evidence, reproducible from the certificates alone:**

    python3 scripts/cva6_corpus_audit.py \
      --frozen <d4>/pass/lean/tests/d4/cva6_30.list \
      --corpus <d4>/temp/cva6_30_auth/blocks \
      --corpus <d4>/temp/cva6_30_auth2/blocks \
      --json   .perfwork/cva6_corpus_audit.json

matching by FLOP IDENTITY (source *i* against `FlopDesc` row *i*):

    sequential 21   any CONFLICT 21   CONFLICT on EVERY flop 16   unknown 0

The five partial cases have a handful of flops with no `flopQAsync` source
(`issue_read_operands` 88+1, `issue_stage` 331+1, `fpu_wrap` 269+6,
`cva6_hpdcache_wrapper` 491+24, `cva6_hpdcache_subsystem` 518+24).

Independent corroboration already on the owning branch:
`DIRECTION4_INCREMENTAL.md:516` records the `btb` case and attributes it to
"the `negreset` conflation the `FlopDesc` docstring warns about".

**Impact here.** 21 of the 30 blocks are sequential.  No RTL-trustworthy
sequential claim can be made from any of them.  A sequential block that passes
the gate is a DIFFERENTIAL result for that certificate -- both sides read the
same contradictory metadata -- and nothing more.

**What would clear it.** One convention decision applied to BOTH emission
paths, then re-emission.  It is a single systematic bug, not 21.  This branch
needs no change: the same certificates, re-emitted, would be re-audited by the
script above.

**Caveat on the scan.** It is a CANDIDATE CONFLICT SCAN.  It reports whether a
certificate's two records of a reset polarity agree.  It does not establish
which one matches the RTL, and it is not a claim about intended semantics.

---

## BLOCKER 2 -- unsupported operators and memory sources

**Owner: this branch**, as ordinary plan work -- Phase 3 (operator coverage)
and Phase 7 (memory).  Nothing upstream is required.

Seven of the 29 audited blocks fail `SupportedByProjection`.  The exact causes,
measured with `cert_shape.py --ops` against `OpSupported`
(`HardwareInterpreter.lean:938`) and `SourceSupported` (`:912`):

| blocker | gates | blocks |
|---|---:|---|
| `Op_MemRead` | 5 | `aes`, `fpu_wrap`, `load_unit`, `store_unit`, `cva6_hpdcache_subsystem` |
| `Op_Mult` | 3 | `fpu_wrap`, `mult`, `multiplier` |
| `Op_MemWriteBE` | 1 | `cva6_hpdcache_subsystem` |
| `memConst` / `memImg` sources | 5 | the same five as `MemRead` |

### The cheap one: `Op_Mult` unlocks two whole blocks

`mult` (3,870 / 4,202) and `multiplier` (2,132 / 2,329) fail on **`Op_Mult`
alone**, and each contains **exactly ONE** `Mult` node.  Their source kinds are
clean -- no memory -- so adding that one operator is the whole fix for them.

That is Phase 3 work in the plan's existing shape: one `Prim`, one `evalPrim`
case calling the pinned `LGraphModel` function, one `rfl` bridge, one
`RequiredArity` entry, then re-run the census.

Both have flops (19 and 6), so they would raise the DIFFERENTIAL count by two
and the no-known-reset-conflict count by zero until Blocker 1 is fixed.

### The expensive one: memory

`MemRead`, `MemWriteBE` and the `memConst`/`memImg` source kinds travel
together across the same five blocks, and `memFree` is a field of
`SupportedByProjection` precisely because `RuntimeState.mems` is
function-valued, so `StateRel`/`ResultRel` admit no memory-bearing state.  That
is Phase 7 and is correctly scheduled after operator coverage.

---

## Suggested order

1. `Op_Mult` -- two blocks, one operator, one node each.  Smallest ratio of
   work to coverage anywhere in this list.
2. Hand Blocker 1 to the exporter owner with the audit output above.  Until it
   lands, 21 blocks can only ever be differential.
3. Memory (Phase 7) -- five blocks, but a representation change to
   `StateRel`/`ResultRel` first.
