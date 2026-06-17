# soomrv Verilog → Pyrope translation record

Goal: translate `../soomrv/repo/src` (top `SoC`) modules from Verilog
(`--reader slang`) to Pyrope (`lhd compile file.v --emit file.prp`), verify each
with `lhd lec` against the original, prefer `comb` > `pipe` > `mod`.  Passing
`.prp` land in `soomrv/pass/`, failures in `soomrv/fail/` (+ a `<name>.md`).

## Pipeline — `soomrv/translate.sh <TOP> <readfiles…>`

1. `lhd compile --reader slang --top TOP --emit-dir pyrope:` → one `.prp`.  The
   prp_writer auto-picks the lambda kind: `comb` (stateless, no submodule),
   `mod` (has a register or instantiates a submodule).  `pipe` is a future
   refinement — nothing is emitted as `pipe` yet.
2. re-compile the `.prp` → `lg:` (writer-fidelity gate).
3. compile the original read set → `lg:` **with the slang flags** (a raw multi-
   file `.sv` passed to `lhd lec` does NOT get the flags, so use-before-declare
   enums break the ref — pre-compiling to `lg:` applies them).
4. lg-vs-lg LEC: **lgyosys (primary)** + cvc5 (secondary).  lgyosys is authoritative
   on lg-vs-lg — no original-reading blind spots and it models `'x` as a true
   don't-care (the in-process cvc5 encoder treats `'x` as concrete and can
   false-REFUTE on `'x`-assigning designs, e.g. TValSelect).  cvc5 array theory
   can also hang on a wide/deep memory, so it is capped at 90 s; lgyosys at 200 s.

**Verdict:** PASS if lgyosys PROVEN, or (lgyosys UNKNOWN/timeout AND cvc5 PROVEN).
LEC-FAIL if either engine REFUTES (and not overridden).  COMPILE-FAIL (reader)
and WRITER-FAIL (`.prp` does not re-compile) short-circuit earlier.

### Submodule handling — `soomrv/stubs/` (`gen_stubs.py`)

`--reader slang` rejects an undefined submodule instance ("blackboxes are not
supported").  `gen_stubs.py` emits a `(* blackbox *)` port-only stub per module
and a `deps.txt` map; translate.sh injects the stubs for TOP's instantiated
children (after the packages, so their port types resolve).  Both LEC sides then
carry the same blackbox Subs.  **Caveat (open):** the slang→prp emit lowers a
blackbox sub to an EMPTY `comb` (it gets inlined on the `.prp` recompile, not
kept as a Sub), so a module that instantiates children currently WRITER-FAILs on
recompile ("call to X has no hardware lowering").  Per-file LEC of hierarchical
modules needs real blackbox-Sub support in the prp writer/reader+lec (M5-level).

Slang flags (from `../soomrv/repo/Makefile`): `--single-unit --std latest
--allow-use-before-declare --relax-enum-conversions --ignore-unknown-modules
--allow-toplevel-iface-ports -Wno-explicit-static -Wno-missing-top
-Wno-multiple-always-assigns`.

## LiveHD fixes made for this task (all regression-clean)

Committed (2 commits):
1. **prp_writer `set_mask`** — render the LHS bit-assign (node 82) as
   `dst#[lo..=hi] = ins` (was `/* TODO: unhandled node type 82 */`, dropping all
   `out[i]=…` logic); non-contiguous masks split per contiguous run.
2. **prp_writer SSA-name rename** — `<base>___ssa_N` → `<base>__wN`, out of the
   recompile's private `___ssa_` namespace (verbatim names collided with the
   re-SSA's own versions → self-assign / dropped writes); per-version-distinct so
   a FIFO's multi-version liveness survives.  + drop redundant self-stores.
3. **upass SSA bare-block inline** — a bare `{ }` block (unconditional nested
   stmts) is inlined into the straight-line SSA scope (rename carry-in/out)
   instead of copied verbatim (which dropped its outer-variable writes).
4. **lec `Set_mask` encoder** — front-end path support (was ABC-netlist only),
   contiguous + non-contiguous constant masks at real_width.
5. **body_has_state** — a submodule `func_call` forces `mod` (a `comb` can't
   instantiate).
6. **collect_folded_attrs** — recurse into nested blocks/if-arms so a memory's
   `mem.[wensize]=N` folds into the `reg mem:…:[…]` declaration (unblocks MemRTL
   family); write_attr_set skips the folded (var,attr).
7. **write_declare** — a no-value combinational var (`T x;`) gets `= 0` (Pyrope
   requires a value); regs keep their bare no-reset form.
8. **write_attr_get** — emit `base.[attr]` (bracket) not `base."attr"` (which the
   parser rejected); unblocks StoreDataLoad + the `.[defer]` read cluster.

## Status — full sweep of 74 modules (8 lib + 66 core), 2026-06-17

**15 PASS** · 41 COMPILE-FAIL · 12 WRITER-FAIL · 6 LEC-FAIL.
Of the 15 PASS: 7 `comb` (stateless) + 8 `mod` (stateful) — the comb>mod
preference is automatic; nothing needed `pipe` (a future refinement).

PASSING (slang→pyrope→lg LEC-equivalent to the original, lgyosys authoritative):

- **lib:** OHEncoder, RangeMaskGen, PrefixSum, PrefixRed, FIFO
- **core:** BranchPredictionTable, CacheWriteInterface, ExternalBus, LoadSelector,
  MemRTL1RW, RenameTable, ResultFlagsSplit, StoreDataLoad, TrapHandler, TValSelect

(comb where stateless, mod otherwise; TValSelect/MemRTL1RW etc. are mod.)

## Remaining failure classes (each a distinct multi-session item)

- **COMPILE-FAIL — reader gaps** (the bulk):
  - `unsupported-lhs-nesting` (nested non-var lvalue `a.b[i].c = …`) — ~16 modules,
    the single biggest reader feature (535 sites in the full SoC).
  - `SimpleAssignmentPattern` / `StructuredAssignmentPattern` as an assignment
    TARGET (`'{…}` on the LHS) — IntALU and others.
  - `HierarchicalValue` (cross-genblock ref `tree[i-1].x`) — PopCnt, MMIO, …
  - `bool != int` operand-type mismatch (the reader compares a 1-bit bool with an
    int const) — AGU, …
  - unpacked-array read on an unsupported base — PriorityEncoder.
  - InterfacePort / `$fflush` — a few.
- **WRITER-FAIL:**
  - hierarchical sub call "no hardware lowering" — needs real blackbox-Sub (above):
    RFReadMux, TagBuffer, CacheReadInterface, DataPrefetch, PrefetchExecutor, …
  - declare placement: a module-scope var first-written in an `if` arm gets its
    `mut … = 0` emitted inside the arm (then used outside → undeclared):
    BranchSelector, InstrAligner.
  - whole-array memory read/write (`OUT_uop` used as a scalar) — InstrDecoder.
- **LEC-FAIL:**
  - PageWalker — refuted by both engines (a real reader/round-trip bug to bisect).
  - MemRTL / MemRTL2W / MemRTLC, Divide, BypassLSU — cvc5 REFUTED (memory/`'x`
    artifact) + lgyosys TIMEOUT (200 s) on the wide/deep memory: LEC-engine limit,
    likely correct (MemRTL1RW, same shape, PASSES) — not confirmed a translation
    bug.  Needs a longer lgyosys budget or a memory-aware LEC.
