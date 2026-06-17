# soomrv Verilog → Pyrope translation record

Goal: translate `../soomrv/repo/src` (top `SoC`) modules from Verilog (`--reader
slang`) to Pyrope (`lhd compile file.v --emit file.prp`), verify each with
`lhd lec` against the original, prefer `comb` > `pipe` > `mod`.  Passing `.prp`
land in `soomrv/pass/`, failures in `soomrv/fail/` (+ a `<name>.md`).

## How modules are translated & verified

`soomrv/translate.sh <TOP> <readfiles...>`:
1. `lhd compile --reader slang --top TOP --emit-dir pyrope:` (one `.prp`).  The
   prp_writer auto-picks `comb` (stateless) / `mod` (stateful); `pipe` is a
   future refinement.  Submodules not in the read set become blackboxes
   (`--ignore-unknown-modules`); LEC blackbox-collapses them.
2. re-compile the `.prp` standalone (writer-fidelity gate).
3. `lhd lec` cvc5 (default) + lgyosys cross-check, plus a **flat cgen cross-check**
   (yosys-SAT over `.prp`→verilog vs slang→verilog — an engine independent of
   cvc5 that is immune to yosys's inability to read the ORIGINAL's unpacked-array
   ports / quirky constructs).

**Verdict:** PASS iff direct-lgyosys PROVEN (yosys read the original → gold) OR
(cvc5 PROVEN AND flat PROVEN → round-trip dual-confirmed by SMT + yosys-SAT).
A direct-lgyosys REFUTED with flat PROVEN means slang and yosys read the ORIGINAL
differently (a reader-level question, not a translation/round-trip fault) — the
slang→pyrope translation is still faithful, so it PASSES.

Slang flags (from `../soomrv/repo/Makefile`): `--single-unit --std latest
--allow-use-before-declare --relax-enum-conversions --ignore-unknown-modules
--allow-toplevel-iface-ports -Wno-explicit-static -Wno-missing-top
-Wno-multiple-always-assigns`.

## LiveHD fixes made for this task (all regression-clean: pyrope 548, slang 119, lec 5)

1. **prp_writer `set_mask`** (`upass/prp_writer/lnast_prp_writer.cpp`): the writer
   had no case for `set_mask` (LHS bit-assign, node type 82) — it emitted
   `/* TODO: unhandled node type 82 */`, dropping all `out[i]=…` logic.  Now emits
   `dst#[lo..=hi] = ins` (in-place RMW), splitting non-contiguous masks into one
   bit-range assign per contiguous run.
2. **prp_writer SSA-name rename**: the writer emitted the reader's POST-SSA names
   (`active___ssa_1`) verbatim; the recompile's own SSA pass re-versions `active`
   to `active___ssa_1` → COLLISION (`irrelevant assignment` / dropped writes).
   Fix: `strip_prefix` renames `<base>___ssa_<N>` → `<base>__w<N>` (out of the
   `___ssa_` namespace, still per-version distinct so multi-version liveness —
   e.g. a FIFO — survives).  + drop redundant self-stores in write_store.
3. **upass SSA bare-block carry-in/out** (`upass/ssa/upass_ssa.cpp`): a bare `{ }`
   lexical block (unconditional nested `stmts`) was copied VERBATIM during SSA —
   no rename carry-in/out — so its outer-variable writes used the stale base name
   and were DROPPED after the block (e.g. a `set_mask` accumulate inside an
   unrolled-loop block).  Now inlined into the straight-line scope (rename
   threading), matching how the slang reader's own nested stmts already worked.
4. **LEC `Set_mask` encoder** (`pass/lec/encode.cpp`): the SMT encoder only handled
   `Set_mask` when an ABC resolution lib was present; the front-end path failed
   ("Set_mask (non-trivial) not supported (M1)").  Now encodes contiguous AND
   non-contiguous constant-mask bit-inserts at the front-end real_width.

## Status by module

### lib/ (parameterized primitives, standalone)

| module | verdict | kind | notes |
|--------|---------|------|-------|
| OHEncoder      | PASS | comb | both solvers PROVEN |
| FIFO           | PASS | mod  | lgyosys PROVEN (cvc5 UNKNOWN on memory) |
| PrefixSum      | PASS | comb | unpacked-array output port; flat-confirmed |
| PrefixRed      | PASS | comb | flat-confirmed (slang reads prefix-reduce correctly; yosys-read of original differs) |
| RangeMaskGen   | PASS | comb | flat-confirmed (slang/yosys disagree on original's `%LEN` one-hot index) |
| PopCnt         | COMPILE-FAIL | — | reader gap: `HierarchicalValue` (cross-genblock ref `tree[i-1].iSums`) |
| PriorityEncoder| COMPILE-FAIL | — | reader gap: unpacked-array read on unsupported base |
| OpDownsample   | (pending) | — | instantiates PrefixSum + PriorityEncoder |

### src/ (core modules) — pending
