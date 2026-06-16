# LiveHD Relational Equivalence Engine (`pass/lec`)

> **Implementation plan — living document.** The section below is the agreed,
> actionable plan (decisions locked 2026-06-14). The phase-by-phase vision and
> the fluid-pipeline brainstorming are preserved verbatim in **Appendix A** as
> the long-term reference. Where the two disagree, the plan here wins.

> **⚠ Architecture update (2026-06-14, supersedes the rows below).** The
> `Pono + smt-switch` discharge stack was **dropped**. `pass/lec` now talks to
> **cvc5 directly via its native C++ API**. Rationale: the M1 encoder is
> combinational-only, so `prove_equal` is just a miter `bad = OR(ref_i !=
> impl_i)` discharged with **one `cvc5::Solver::checkSat`** (UNSAT = equal, SAT =
> witness) — the Pono transition-system/latch/BMC apparatus was a no-op wrapper
> around that single query. Dropping the two layers also removed an fmt-12 source
> incompatibility (pono) and the `rules_foreign_cc` CMake build (smt-switch).
> cvc5 stays the **prebuilt non-GPL static** lib (`packages/cvc5_repo.bzl`,
> per-host: Linux x86_64 / Linux arm64 / macOS arm64); GMP is now the hermetic
> `@gmp` BCR module (no system/homebrew libgmp). **When unbounded *sequential*
> equivalence (designs without flop correspondence) is actually needed, re-add a
> BMC unroll on cvc5, or re-introduce Pono.** Everything past M1 in this doc that
> assumes a model checker should be re-read with that in mind.

## Decision record (2026-06-14)

| Decision | Choice |
|---|---|
| Primary use case | A *small edit* to a few `.prp` files in a large repo, re-verified **fast** by checking only what changed |
| Baseline model | Explicit `--ref` / `--impl` each run; **no persistent certificate cache in v1** (the original Phase 6 is deferred) |
| Reduction granularity | Per-**def** structural diff: collapse structurally-identical defs, hand only the changed defs to the solver |
| Discharge engine | ~~Pono, in-process via its C++ API~~ → **cvc5 directly (native C++ API), single `checkSat` on a combinational miter.** See the Architecture update banner above. |
| Solver backend | **cvc5** (modified BSD-3) as the single backend, used **directly** (no smt-switch) — cvc5 ships official **prebuilt static libs** for Linux x86_64 / Linux arm64 / macOS arm64, so *no from-source solver build*; covers QF_BV (and QF_ABV for future memory work). GMP is the hermetic `@gmp` BCR module. bitwuzla is a possible later `lec.solver=bitwuzla` opt-in. |
| `lgcheck` (yosys `equiv`) | Kept **only as a bring-up cross-check oracle.** The goal is to *replace* it — it does not scale beyond combinational logic |
| Proof strategy | Modular / congruence skeleton **plus preserved flop & memory names as first-class cut-points** (register-correspondence SEC: a sequential proof collapses to a combinational base+step between matched state) |
| Core abstraction | A **relational query engine**: `prove_equal` / `prove_distinct` / `is_sat` over *subsets* of one or two designs. Incremental LEC and design queries (e.g. "are these two memory-port addresses always / never equal?") are both **clients** |
| Engine tuning | Pono's main switches are exposed as `lec.*` set-options through the existing `lhd list options` registry — e.g. `lhd lec --set lec.timeout=90 --set lec.engine=ind`; unknown `lec.*` flags hard-error (no silent no-op) |
| Milestone 1 | **DONE.** Encode **one combinational module** to cvc5 bit-vector terms, prove a known-equal and a known-different pair via `cvc5::checkSat`, cross-checked against `lgcheck` (`//pass/lec:comb_equiv_test`, `:cvc5_link_test`, `:lec_cross_test` all green). |

**Explicitly out of v1:** sequential k-induction (M2), structural def-diff
reduction (M3), memory queries (M4), hierarchical congruence + CEGAR (M5),
persistent cache (M6), and the **elastic / fluid-pipeline** transaction-level
equivalence — that last one stays long-term research (Appendix A, "Fluid
Pipeline").

---

## What we are building

Not a LEC tool — a **relational query engine over LiveHD `LGraph`s**. The
question it answers is *"what is the relationship between these two subsets of
logic (in one design, or across two designs)?"*:

- `prove_equal(a, b)` — are signals/cones `a` and `b` equal for all reachable states & inputs?
- `prove_distinct(a, b)` — are they *guaranteed never* equal?
- `is_sat(predicate)` — is some relation reachable at all? (returns a witness)

Two first clients motivate the whole effort:

1. **Incremental LEC** (the headline): `prove_equal(impl.outputs, ref.outputs)`
   under `assume_equal(inputs)`, made fast by only re-proving the *changed*
   defs. This is what eventually backs `lhd lec`.
2. **Design queries for other passes**: e.g. a memory-port optimizer asks
   `prove_equal(portA.addr, portB.addr)` to merge ports, or
   `prove_distinct(write.addr, read.addr)` to license a no-forwarding
   assumption. Same engine, same encoder, different query.

---

## Architecture (layers)

```
   L3  Orchestration            pass.lec  /  lhd lec  /  pass-facing query helpers
        |  structural diff -> reduced set of queries -> report / certificate
        v
   L2  Correspondence & reduction
        |  def structural-hash (skip identical defs)
        |  preserved flop/memory NAME matching -> 1:1 state map -> induction cut-points
        |  modular congruence (changed leaf proven in isolation; parents inherit)
        v
   L1  Query API               prove_equal / prove_distinct / is_sat   (over 1 or 2 designs)
        |  builds a miter / relation in the transition system, picks an engine,
        |  runs it, returns Proven / Refuted(+witness) / Unknown
        v
   L0  Encoder                 LGraph  ->  pono::TransitionSystem (smt-switch terms)
        |  comb node -> term ; flop -> state var + next-state ; memory -> SMT array ;
        |  module IO -> input/term ; Sub-instance -> inlined or uninterpreted boundary
        v
       Pono (BMC / k-induction / IC3-PDR)  +  smt-switch  +  bitwuzla
```

- **L0 Encoder** is the big reusable piece. One `LGraph` (or a pair, for a
  miter) becomes a `pono::RelationalTransitionSystem` whose terms are
  `smt::Term`s. It is deterministic and side-effect free.
- **L1 Query API** is the product surface. It assembles the relation
  (`assume`/`assert`), chooses an engine, and reports a typed result.
- **L2** is what makes it *fast*: it shrinks the work L1 has to do.
- **L3** is the thin CLI/pass glue (mirrors the existing `pass.color` /
  `pass.partition` / `lhd lec` plumbing).

---

## Milestones

Each milestone ends green (built + a passing test) before the next starts.

### M1 — Thin combinational query slice  *(de-risk the build)*  ← FIRST

Goal: prove the toolchain links and the encoder is sound on the simplest case.

1. **Bazel: vendor + link the solver stack.** `bitwuzla` (+ GMP, CaDiCaL,
   SymFPU), `smt-switch` (bitwuzla backend), `pono` — as `http_archive` +
   custom `BUILD` (the pattern LiveHD already uses for `abc` / `yosys` in
   `MODULE.bazel`), or `rules_foreign_cc` cmake wrappers. *Build approach +
   exact pins decided in "Build & dependency notes".*
2. **L0 (comb only).** New `pass/lec/encode.{hpp,cpp}`: walk a single
   combinational `LGraph` in topo order, map each cell op (the Dlop/Hlop op
   set) to an `smt::Term`, with correct bit-width and signedness. No flops, no
   memory, no hierarchy yet. Reference: `inou/cgen` is the existing
   netlist-walker to mirror.
3. **L1 (stateless).** New `pass/lec/query.{hpp,cpp}`: `prove_equal(a,b)` /
   `prove_distinct(a,b)` over an encoded comb design → a single `check_sat` on
   the miter (no engine/temporal reasoning needed yet). Result type
   `{Proven, Refuted(witness), Unknown}`.
4. **Cross-check harness.** A `pass/lec/tests/` test that takes two tiny
   combinational modules (one truly equal pair, one off-by-one pair), runs the
   query, and asserts the verdict **also matches `lgcheck`** on the same pair.
5. **L3 stub.** `lhd lec --impl … --ref …` wired enough to drive (2)-(4); no
   reduction yet (it just encodes + queries whole comb modules).

**M1 done =** `bazel test //pass/lec/...` green, verdicts agree with `lgcheck`.

### M2 — Sequential via register correspondence
Encode the SEC as one transition system: `Init = both resets`, `Trans = both
next-state functions`, `Bad = Or of output XORs`. Match flops **by preserved
name** across impl/ref → a 1:1 latch map → base case at reset + inductive step
(assume mapped-register equivalence at cycles 0..k, prove it + output agreement
at k+1), discharged with **k-induction first** (Pono
`KInduction`/`BmcSimplePath`), BMC for shallow CEX, IC3/PDR to close what
induction can't. Seed correspondence candidates by simulation, confirm by SAT,
and refine via the **van Eijk greatest-fixed-point** (drop any pair whose
next-states aren't provably equal — one-sided / false-negatives-only, so always
sound). Test: a flop pipeline, edit that preserves flop names.

### M3 — Structural def-diff reduction  *(the "fast incremental" headline)*
L2 structural hashing: canonical per-def hash (op + params + sorted
connectivity + interface). Compare the `--ref` and `--impl` libraries
def-by-def; identical defs are dropped with **no solver call**. Only changed
defs (and parents whose child *interface* changed) become queries. This is
where a 1000-def design with a 1-def edit checks ≈1 def. Test: the existing
`hier_*` fixtures with a single-def edit; assert only that def was solved.

### M4 — Memory modeling + memory queries
Memory cells → SMT **theory of arrays** (`make_sort(ARRAY,…)` + `Select`/`Store`,
read-over-write + extensionality). Two abstraction levels: full array state, or
the cheaper **interface/transaction equivalence** (prove the read/write
addr+data+enable *streams* equal rather than array contents) — the latter is
unsound if forwarding, latency, or array→physical-memory mapping differ, so
guard those conditions. Exposes the memory-port/forwarding
`prove_equal`/`prove_distinct` queries as the second client ("are these two
ports' addresses always equal?" → merge; "guaranteed never equal?" → no-forward
license). Optional: tiny memories expanded to flops.

### M5 — Hierarchical congruence + CEGAR
**Landed (combinational inline flattening):** a `Sub` instance whose def is
*combinational* is flattened inline by the encoder — the def is encoded with its
inputs bound to the instance's input Vals and its outputs wired onto the
instance's output pins (`Encoder::set_sub_lib`, a name-hash `Gid -> def graph`
map). The map is supplied by `lhd lec --lib lg:DIR` (repeatable); the prime use
is an ABC standard-cell netlist whose blackbox cell `Sub`s resolve to the
`pass.liberty gensim` cell models. Such a mapped netlist also drives its
multi-bit IO boundary with `Get_mask`/`Set_mask`, both now fully encoded
(contiguous-mask bit-select / bit-insert); its unsigned nets carry no spare sign
bit, so under `--lib` the encoder uses the **raw** `bits_of` width there instead
of the front-end magnitude+1 `real_width`. Unresolved / stateful / too-deeply
nested defs keep the sound `Sub -> Unknown`. The assume-guarantee skeleton below
is the remaining (sequential / black-box) work.

Assume-guarantee: prove a changed leaf def equivalent **in isolation with free
inputs** (free-input isolation is conservative → always sound; a
reachable-input-only proof is *unsound* for congruence), then black-box it in
the parent (its inputs become compare points, its outputs free source points on
both sides) and conclude the parent equivalent without re-proof. **Soundness
side-conditions** (auto-checked, else flatten the region): identical interface +
timing, no new combinational path across the boundary, discharged environment
assumptions, full I/O equivalence. Retiming or a changed interface breaks the
1:1 map → flatten and re-prove. On a spurious CEX from an isolated/abstracted
proof, expand the cone / un-black-box / re-add the cut-point and retry (CEGAR —
the original Phase 8 loop).

### M6 — Persistent certificate cache  *(original Phase 6)*
Key = canonical hash + interface hash + assumptions; value = certificate. "Prove
once, reuse forever" across runs/passes. Deferred until M1–M5 prove out.

### Deferred indefinitely — elastic / fluid-pipeline equivalence
Transaction-level (accepted-stream) equivalence for `valid`/`retry` interfaces.
Real model checking (not yosys `equiv`) is a prerequisite, which is exactly why
the engine is Pono-based — but the encoding work is its own research track
(Appendix A, "Fluid Pipeline Equivalence Brainstorming").

---

## Milestone 1 — detail

**New files (mirroring `pass/color`, `pass/partition` layout):**

```
pass/lec/
  BUILD                      # cc_library + cc_test, deps on @pono//:pono etc.
  pass_lec.{hpp,cpp}         # Pass_plugin "pass.lec" (L3 entry; thin)
  encode.{hpp,cpp}           # L0: hhds::Graph (Ntype_op) -> pono::FunctionalTransitionSystem (comb subset)
  query.{hpp,cpp}            # L1: prove_equal / prove_distinct / is_sat + Result type
  tests/
    comb_equiv_test.cpp      # equal pair + off-by-one pair, cross-checked vs lgcheck
```

**`lhd lec` CLI (L3):** add to `lhd/lhd_kernel.cpp` next to `check_command`,
taking `--impl KIND:PATH` / `--ref KIND:PATH` like `lec` (reuse
`materialize_verilog`'s front half to get `LGraph`s, but stop *before* cgen —
we want the graph, not Verilog). v1 just encodes + queries whole comb modules.
Register `{"lec", "pass.lec"}` in `kSetPasses[]` (lhd/lhd_kernel.cpp:313) and
call `merge_sets(opts, "lec", labels)` in **both** `check_command` and the new
`lhd lec` path so the `lec.*` engine switches flow in (see "Engine options"
below).

**Encoder op coverage (M1 subset):** the op enum is `livehd::Ntype_op`
(`graph/cell.hpp`, 27 real values) — *not* hlop's Dlop. M1 needs: `Nconst`,
`And/Or/Xor/Not`, `Sum` (a-pins add, b-pins **subtract**), `Mult`, `Div`,
`LT/GT/EQ` (GE/LE/NE are negations — no separate op), `Mux` (sel pid0, arms
pid1..N) / `Hotmux` (one-hot sel) / `LUT`, `SHL` (b-amount is one-hot
multi-driver, ORed) / `SRA` (**arithmetic** rshift — there is no logical
rshift), `Get_mask` (`(a,-1)` = zero-extend) / `Set_mask`, `Sext`. Map each to
the matching smt-switch `Op` (`BVAdd`, `Concat`, `Extract`, `Zero_Extend`,
`Ite`, `BVUlt`, …). **The width rule is the #1 trap** (see Encoder reference
below). Mirror `inou/cgen/cgen_verilog.cpp::process_simple_node` for exact
per-op semantics.

**Pono wiring (M1) — the key gotcha:** build a `FunctionalTransitionSystem
fts(solver)`; encode both designs' combinational logic into `smt::Term`s
sharing the input vars; miter `bad = Or(Xor(impl_out_i, ref_out_i))`. A Pono
`SafetyProperty` may only reference *current-state* vars — a purely
combinational `bad` over inputs/outputs makes `Bmc` return `UNKNOWN`. So
**latch the miter into a 1-bit state var**: `fts.assign_next(bad_state, bad)`
and assert `SafetyProperty(solver, Not(bad_state))`. Run
`Bmc(prop, fts, solver).check_until(1)`; `ProverResult` is `TRUE=1`
(`prove_equal` holds), `FALSE=0` (`witness(vec)` yields the differing input
assignment), `UNKNOWN=-1`. `prove_distinct` is the dual (prove `Not(Equal(a,b))`
invariantly); `is_sat(pred)` returns a witness. Engines share the ctor
`(SafetyProperty, TS, SmtSolver, PonoOptions, Engine)`; use `SafetyProperty`,
not the deprecated `Property`.

**Query result type:**
```cpp
enum class Verdict { Proven, Refuted, Unknown };
struct Query_result {
  Verdict           verdict;
  std::string       witness;   // when Refuted: a satisfying assignment / CEX
  std::string       detail;    // engine, bound, time
};
```

**Test oracle:** the same two module pairs go to (a) our `prove_equal` and (b)
`lgcheck`; the test fails if they disagree. This is the trust anchor while the
encoder is young. The strongest encoder check (research rec): emit *both* the
SMT model and the cgen Verilog from the **same** graph and LEC them via
`lgcheck` — any disagreement is an encoder bug, caught before it can mask a real
design diff.

---

## Milestone 1 — STATUS: COMPLETE (2026-06-14)

`bazel test //pass/lec/...` is green (5 tests), and `lhd lec --set lec.cross=true`
verdicts agree with `lgcheck` on equal + different pairs. Files shipped:
`pass/lec/{encode,query,pass_lec}.{hpp,cpp}` + `BUILD`, tests
`tests/{cvc5_link,smtswitch_link,pono_link,comb_equiv}_test.cpp` +
`tests/lec_cross_test.sh`. CLI: `lhd lec --impl … --ref …`, `--set lec.*`
options registered (`lhd list options 'lec\..*'`), typos hard-error.

**Build deviations from the original sketch (these shipped; plan reconciled):**

- **cvc5** (`packages/cvc5.BUILD`, pin `cvc5-1.3.4` Linux-x86_64 prebuilt
  `-static.zip`): the prebuilt `libcvc5.a` + `libcadical.a` + LibPoly are merged
  into **one relocatable object** (`ld -r --whole-archive`) and the **CaDiCaL
  symbols are localized** (`objcopy --localize-symbol '*CaDiCaL*'`). Reason:
  **Berkeley-abc** (linked into the same `lhd` binary via the Yosys flow) **also
  vendors CaDiCaL** → duplicate strong symbols at the final link. Localizing
  cvc5's private copy fixes it (same idea as `lhd_export.lds` for slang/fmt/boost).
  `libcvc5parser.a` is dropped (the C++-API backend never uses the SMT-LIB
  parser, and it collides on member basenames during the merge). GMP stays
  **dynamic** (`-lgmp`/`-lgmpxx`). NB: `ar x` flattens duplicate member
  basenames — must use `ld -r --whole-archive`, never extract.
- **smt-switch** (`packages/smtswitch.BUILD` + `smtswitch.patch`, pin **v1.1.3**,
  `rules_foreign_cc` cmake): built against cvc5 **headers only**
  (`@cvc5//:cvc5_headers`) — a STATIC `smt-switch-cvc5` has no link step, so the
  patch drops the `find_library(... REQUIRED)` checks + per-lib link records and
  **disables the ar-repack** (cvc5 comes from `@cvc5` downstream, kept a single
  localized copy). `CVC5_HOME` is passed as `$$EXT_BUILD_DEPS` (leading `$$`
  only — a trailing `$$` leaves a stray `$`).
- **pono** (`packages/pono.BUILD` + `pono.patch`, pin main `ec4fe89`): built
  **directly with Bazel** (a hand `cc_library`, like abc/yosys), **not** its own
  CMake. Pono's CMake hard-requires bitwuzla + Btor2Tools + flex/bison, but all
  of that is in the file `frontends/` (and the witness `printers/`), which
  pass/lec does not use (it builds the `TransitionSystem` in memory). We glob
  `core/engines/smt/utils/options/modifiers/refiners`, exclude
  `frontends/`, `printers/`, `engines/msat_ic3ia.cpp` (MathSAT, off-limits), and
  `pono.patch` gates the bitwuzla references in `smt/available_solvers.cpp`
  behind `WITH_BITWUZLA` (undefined here). Force-include `<cassert>`/`<cstdint>`
  (libstdc++ no longer pulls them transitively).
- **L1 discharge**: combinational miter latched into a 1-bit state var, property
  `Not(bad_state)`, default engine **k-induction** (`lec.engine=ind`) which gives
  a definitive `Proven` (BMC alone returns UNKNOWN on no-CEX); BMC/IC3 selectable.
- **`lhd lec`** lives in `lhd_kernel.cpp::lec_command` and discharges via
  `lec::prove_equal` directly (clean `equiv_fail` error class + witness);
  `pass.lec` is registered for the `lec.*` option registry and is also a working
  dispatch entry (`graph[0]`=ref, `graph[1]`=impl). Inputs: `verilog:`/`lg:`/
  `pyrope:`/`ln:` (verilog elaborates through `--reader`, default slang; or use
  `--set lec.solver=lgyosys` for the yosys/lgcheck backend).

M2–M6 remain as below (unstarted).

## Encoder reference (L0) — LiveHD graph facts

The post-elaboration netlist is an `hhds::Graph` (external dep); the op enum and
the LiveHD conveniences live in `graph/cell.hpp` + `graph/node_util.hpp`. The
encoder is "`cgen` that emits SMT instead of Verilog" — same visit order, same
per-op switch. Depend on `//graph:graph` + the hhds module; reuse
`graph_util::*` verbatim (do **not** re-derive pin/bit/sign conventions).

**Walk** (mirror `cgen_verilog.cpp::do_from_graph`): module IO from
`Graph::get_io()` (`DeclaredIoPin{name,port_id,bits,unsign}`) → combinational
nodes in topo order via `Graph::forward_class()` → collect `Memory`/`Sub`/`Flop`
via `Graph::fast_class()` filtered by `graph_util::type_op_of(node)`. The
`INPUT/OUTPUT/CONST` singletons are never yielded — reach IO via `get_io()`;
constants appear only as `edge.driver` pins (`graph_util::hydrate_const(pin) -> Dlop`).

**Op classification:** `graph_util::type_op_of(node) -> Ntype_op`. It checks
`get_subnode_gid() != Gid_invalid` first → `Sub` (authoritative). **Never trust
`get_type()` for subs** — `set_subnode` re-stamps the raw type field and value 2
collides with `Sum`.

**Pins / operands:** named sinks (`"a"`,`"b"`,`"din"`,`"clock_pin"`,…) map to a
numeric `Port_id` via `Ntype::get_sink_pid(op,name)`; use
`graph_util::find_sink_pin` / `get_driver_of_sink_name` / `inp_drivers_of`
(multi-driver). For `Sub`, sink/driver names come from the child GraphIO — use
`node.get_sink_pin(name)` / `get_driver_pin(name)`. Single-driver output is
`get_driver_pin(0)`.

**⚠ Bit-width — the #1 trap:** the per-pin `bits` attribute
(`graph_util::bits_of`) is the **signed** count = magnitude + 1 (one sign bit),
*regardless* of the sign flag. The real SMT bit-vector width is
`w = graph_util::is_unsign(pin) ? bits_of(pin) - 1 : bits_of(pin)` — a 4-bit
unsigned bus is stored as `bits=5`. `is_unsign(pin)` ≡ the `pin_signed` attr is
absent. Off-by-one here silently breaks every comparison/arith equivalence.
(cgen proves it: `reg signed [bits-1:0]` for signed, `bits-1` for unsigned,
`cgen_verilog.cpp:1334`.)

**Op-semantics traps (build the table from the 27 real `Ntype_op`s, per
`process_simple_node`):** `Sum` — a-drivers add, **b-drivers subtract**. `SRA` —
arithmetic (sign-preserving); there is **no** logical-rshift op. `SHL` — the `b`
amount is **one-hot multi-driver**: `(v<<b0)|(v<<b1)|…`. `Get_mask(a,-1)` =
zero-extend; contiguous mask = part-select (`Dlop::get_mask_range`). `Mux` —
`sel` pid0, arm `i` chosen when `sel==i-1`; `Hotmux` — one-hot `sel` (add the
one-hot constraint or model the don't-care). `LT/GT` can be multi-input (all
a-vs-b pairs ANDed). **Decomposed at tolg, so absent as primitives:** `Mod`,
reduce-AND/XOR, popcount, `GE/LE/NE`, logical-rshift — they appear as
`Ror/Not/And/Get_mask/SRA/EQ` compositions. `AttrSet` = no-op pass-through.

**Flop (`Ntype_op::Flop`)** → one SMT state var. Pins: `0 async, 1 initial
(reset value), 2 clock_pin, 3 din, 4 enable, 5 negreset, 6 posclk, 7 reset_pin,
8 pipe_min, 9 pipe_max`. `next = enable ? din : q` (enable false = hold);
`reset_pin` active → `q' = initial` (default 0); `pipe_min`>1 models an N-deep
shift register (N state vars). Output `q = driver_pin(0)`. (M2.)

**Memory (`Ntype_op::Memory`)** → SMT **theory of arrays**
(`make_sort(ARRAY,idx,elem)` + `Select`/`Store`). **12-pin port stride**: port
*k*'s pins are at `pid + 12*k` (`get_sink_name` does `pid % 12`); per-port
logical pins `0 addr, 2 clock_pin, 3 din, 4 enable, 10 rdport (1=read/0=write)`;
comptime pins `1 bits, 5 fwd, 6 posclk, 7 type (0 async-rd / 1 sync-rd /
2 array-ROM), 8 wensize, 9 size, 11 init`. Read-data output for read port *N* =
`driver_pin(wr_ports + N)`. (M4.)

## Engine options via `lhd list options`

Pono exposes many knobs (engine, solver backend, bound, timeout, …). The main
ones are surfaced through LiveHD's **existing** set-option registry so they work
uniformly on `lhd lec` (the single equivalence command) — same machinery
`pass.color` / `pass.partition` already use:

```
lhd lec --impl … --ref … --set lec.engine=ind     --set lec.timeout=90
lhd lec --impl … --ref … --set lec.solver=bitwuzla --set lec.bound=20
lhd lec --impl … --ref … --set lec.solver=lgyosys  # the yosys/lgcheck backend (was `lhd check`)
```

Wiring (three touch-points, all derived from one table):

1. The `pass.lec` Eprp method's `add_label_optional(...)` calls **are** the
   switch registry — one per exposed knob. `lhd list options 'lec\..*'` and
   `lhd describe lec.<flag>` derive from these automatically
   (`list_set_options()`, lhd/lhd_kernel.cpp:2350).
2. Add `{"lec", "pass.lec"}` to `kSetPasses[]` (lhd/lhd_kernel.cpp:313) so
   `--set lec.X` and `--config [lec]` are recognized and **typos hard-error**
   via `check_known_set_passes` (lhd/lhd_kernel.cpp:361) — never a silent
   no-op.
3. `merge_sets(opts, "lec", labels)` in `check_command` and the `lhd lec` path
   feeds the flags into the pass.

Initial `lec.*` switches:

| Flag | Meaning | Default |
|---|---|---|
| `lec.engine`  | discharge engine: `bmc` \| `ind` (k-induction) \| `ic3` | `ind` |
| `lec.solver`  | smt-switch backend: `cvc5` \| `bitwuzla` | `cvc5` |
| `lec.timeout` | per-query wall-clock seconds (`0` = none) | `0` |
| `lec.bound`   | BMC / induction depth bound `k` | `20` |
| `lec.witness` | print the counterexample/witness on `Refuted` | `true` |
| `lec.cross`   | also run `lgcheck` and assert agreement (bring-up only) | `false` |

The set grows with the milestones (M4 adds memory-abstraction knobs, etc.).
Note: `ic3`'s interpolation variant (`ic3ia`) needs MathSAT, which is not
redistributable — with a permissive backend only the non-interpolating engines
are exposed (see Build notes). `--set lec.timeout=90` is the canonical example.

---

## Build & dependency notes  *(dominant risk — researched 2026-06-14)*

The stack is `pono` → `smt-switch` → an SMT backend, all BSD-3 / permissive
except the caveats below. `rules_foreign_cc` **0.15.1 is already a `bazel_dep`**
in `MODULE.bazel`, and LiveHD already vendors `abc`/`yosys`/`slang` via
`http_archive` + a hand-written `packages/*.BUILD` (+ patches) — there are even
**dormant `packages/boolector.BUILD` + `packages/cryptominisat.BUILD`** from a
prior SAT-solver integration to use as references.

**Backend = cvc5 (decided).** cvc5 is modified BSD-3, CMake, and — decisively —
ships **official prebuilt non-GPL static libraries every release** for Linux
(x86_64/arm64), macOS (arm64/x86_64), and Windows. So cvc5 needs **no
from-source build**: `http_archive` the `cvc5-<os>-<cpu>-static.zip` (the plain
one, *not* `-static-gpl.zip`) + `cc_import` of `lib/libcvc5.a` + headers,
`select()`-ed per OS/CPU (mirrors the dormant `boolector.BUILD`). It covers
**QF_ABV** (bit-vectors + arrays) and every engine we need (BMC, k-induction,
IC3/PDR). bitwuzla (MIT) is faster on pure BV but uses **Meson** (not CMake),
must compile from source, and **auto-downloads CaDiCaL/SymFPU** (breaks Bazel's
hermetic sandbox) — defer it behind `lec.solver=bitwuzla`.

**Licensing (LiveHD is BSD-3 and ships binaries → deps must be redistributable):**
- cvc5 / smt-switch / pono — BSD-3 / permissive ✅ (keep cvc5 default `--no-gpl`; never the `-static-gpl` artifact; CLN/GLPK/CoCoALib stay disabled).
- **GMP is LGPLv3** and is linked by every BV backend — **link it dynamically** (system/shared `libgmp`, *not* the static `.a`) to keep the LGPL relink obligation satisfiable; list it in `NOTICE`.
- **MathSAT is proprietary / non-redistributable** → Pono's interpolation engines (`IC3IA`/`InterpolantMC`) are **off-limits for the shipped binary**. We use **BMC + k-induction + IC3/PDR**, none of which need an interpolating solver. Gate any IC3IA path behind an off-by-default flag so the binary never links MathSAT.

**Build sequence (each step its own green checkpoint):**
1. `http_archive` + `cc_import` the prebuilt **cvc5** static lib + a `cc_test` doing `Cvc5SolverFactory::create(false)` + a `QF_ABV` `check_sat`. ← proves the link.
2. `rules_foreign_cc` `cmake()` **smt-switch** against the prebuilt cvc5 (`-DBUILD_CVC5=ON` / `--cvc5-home`), `out_static_libs=[libsmt-switch.a, libsmt-switch-cvc5.a]`. Pin **smt-switch v1.1.3** (the `PrimOp`/`Op(prim,idx0,idx1)` ABI is version-sensitive).
3. `rules_foreign_cc` `cmake()` **pono** on top.
4. Wire `pass/lec` to build the in-memory transition system + call Pono `Bmc`/`KInduction` (Milestone 1 detail).

**Cross-check oracle:** keep `inou/yosys/lgcheck` reachable from the tests (it
already builds); the encoder is validated against it but it is **never** on the
production path.

---

## Open questions / to firm up

- **Resolved by research:** backend = **cvc5** (prebuilt static, no from-source
  solver build); pins smt-switch **v1.1.3**, cvc5 ≥ **1.3.4**, pono master;
  MathSAT/IC3IA excluded; GMP dynamically linked. Encoder consumes `hhds::Graph`
  directly (like cgen).
- Exact pono commit to pin (master moves) + whether to vendor or
  `git_repository` it.
- Canonical def-hash definition for M3 (op + params + sorted edges + interface)
  — must be stable across two independent builds of an unchanged def.
- How `--ref` / `--impl` are supplied for the "two git revisions" workflow
  (two `lg:` dirs now; `--ref-rev` convenience later).
- Does cvc5's BV+array performance suffice, or do we need the bitwuzla opt-in
  sooner (revisit if M2/M4 proofs are slow).

---

## References (research, 2026-06-14)

- **Pono C++ API** — `core/{fts,rts,ts,prop}.h`, `engines/bmc.h`,
  `smt/available_solvers.h`, `tests/test_witness.cpp`, `tests/test_ic3ia.cpp`,
  `examples/python-api/simple_alu.py` · github.com/stanford-centaur/pono (BSD-3)
- **smt-switch** — `include/smt_defs.h`, `include/ops.h`, cvc5 factory ·
  github.com/stanford-centaur/smt-switch (pin **v1.1.3**)
- **cvc5** prebuilt static libs · github.com/cvc5/cvc5/releases (modified BSD-3,
  default `--no-gpl`) · **rules_foreign_cc** · github.com/bazel-contrib/rules_foreign_cc
- **LiveHD encoder surface** — `graph/cell.hpp` (`Ntype_op`),
  `graph/node_util.hpp` (`graph_util::*`),
  `inou/cgen/cgen_verilog.cpp` (reference walker / `process_simple_node`),
  `inou/yosys/lgyosys_tolg.cpp` (Flop/Memory/Sub builder),
  `pass/bitwidth/bitwidth.cpp` (`bits` = signed count = magnitude+1),
  `packages/boolector.BUILD` (dormant SAT-solver Bazel vendoring reference)
- **SEC methodology** — van Eijk register correspondence (TCAD'00); Cheng SEC
  (HLDVT'05); Sakallah CEC; ABC `dsec`/`dprove` (FRAIG SAT-sweeping); Pono 2.0
  (FM) — see citation URLs in the workflow research log.

---

# Appendix A — Original Design Notes & Long-Term Vision

*(Everything below predates the plan above. Kept as the conceptual reference and
the long-term roadmap — including the fluid-pipeline research track. Where it
conflicts with the plan above, the plan above wins.)*

## Motivation

Traditional SAT/SMT-based equivalence checkers begin with RTL or an AIG and must rediscover structural and functional correspondence. LiveHD has substantially more information:

- LGraph hierarchy
- Stable node identities
- Transformation history
- Optimization provenance
- Memory objects
- Register structure
- Existing structural hashes

The goal is to exploit this information to reduce the verification problem from *whole-design equivalence* to *equivalence of only the changed regions*.

The resulting reduced problem can then be discharged using Pono (BMC, induction, IC3/PDR).

---

# High-Level Algorithm

```
            LGraph A                  LGraph B
                |                         |
                +-----------+-------------+
                            |
                  Structural Matching Pass
                            |
                +-----------+------------+
                |                        |
          structurally equal       unmatched regions
                |                        |
           collapse/share           recursive matching
                |                        |
                +-----------+------------+
                            |
                    Recursive BMC (Pono)
                            |
                proven equivalent regions
                            |
                      collapse + cache
                            |
                     reduced global proof
```

The philosophy is:

- aggressively collapse everything that can be proved
- recursively verify only remaining unmatched regions
- remember successful proofs permanently

---

# Phase 1: Structural Traversal

Perform a top-down traversal.

For every pair of nodes:

- identical operation
- identical parameters
- identical latency
- identical interface
- identical children

mark as:

```
StructurallyEquivalent
```

Collapse into a shared symbolic object.

No SAT/BMC invocation required.

---

# Phase 2: Memory Recognition

Memories deserve special treatment.

Detect:

- identical width
- identical depth
- identical masks
- identical latency
- identical read/write semantics
- identical initialization semantics

Possible reductions:

## identical memory

```
memA
memB

    =>
      shared_memory
```

No memory equivalence proof required.

## interface-only proof

Instead of proving

```
memA == memB
```

prove

```
write_addr
write_data
write_enable

read_addr
```

are identical.

Use one shared SMT array.

Avoid duplicated memory state.

Tiny memories may optionally be expanded into flops.

---

# Phase 3: Register Recognition

Detect:

- matching flops
- identical reset
- identical initialization
- identical enable
- identical clock domain

Collapse into shared state variables whenever possible.

---

# Phase 4: Recursive Functional Matching

Remaining unmatched regions:

```
           parent

        /    |     \

      good  changed good
```

Recursively descend only into changed regions.

Attempt:

- structural
- canonicalization
- algebraic simplification

before invoking BMC.

---

# Phase 5: Recursive Pono Verification

For remaining candidate regions:

Construct a local miter.

```
inputs equal

        |

    region A

    region B

        |

assert(outputs equal)
```

Run:

- bounded model checking
- k-induction
- IC3/PDR (optional)

If equivalent:

```
collapse(region)
```

Replace with shared symbolic node.

Continue upward.

---

# Phase 6: Persistent Equivalence Cache

A major optimization.

If BMC proves

```
Block X == Block Y
```

store a persistent record.

Example:

```
(signatureA,
 signatureB,
 assumptions,
 latency,
 reset semantics)
        ->
certificate
```

Future optimization passes should consult this cache before invoking Pono.

Possible key:

```
canonical hash
+
interface hash
+
semantic version
+
optimization assumptions
```

The cache survives repeated optimization iterations.

Goal:

```
prove once

reuse forever
```

---

# Phase 7: Provenance-Based Matching

Every optimization should preserve provenance.

Example:

```
Node 51

    |

constant propagation

    |

Node 74
```

Maintain

```
derived_from
optimization
transformation id
```

This provides extremely strong hints for correspondence.

BMC should be the last resort, not the first.

---

# Phase 8: Incremental Refinement

Initially collapse aggressively.

If proof fails:

- expand affected region
- rerun BMC
- recursively refine

Equivalent to a CEGAR-style refinement loop.

---

# Potential Future Extensions

## FRAIG-style functional hashing

Maintain functional representatives for proven-equivalent nodes.

## SAT sweeping

Merge nodes after local proofs.

## Incremental SAT/BMC

Reuse learned clauses across optimization iterations.

## UNSAT-core localization

Reduce proof cones automatically.

## Interpolation-based reduction

Automatically discover minimal required state.

## Hierarchical certificates

Store proofs for reusable IP blocks.

---

# Design Philosophy

The verification complexity should scale approximately with:

```
size(changed_region)
```

rather than

```
size(entire_design)
```

The primary objective is to exploit LiveHD's internal knowledge so that generic SMT/model checking is only applied to the irreducible functional differences.


# Fluid Pipeline Equivalence Brainstorming

## Motivation

Many LiveHD interfaces are *elastic* (fluid): values are only meaningful when accompanied by a `valid` signal. A complementary `retry` signal indicates that the presented value was **not consumed** and must remain stable until it is eventually accepted.

For these interfaces, cycle-by-cycle equivalence is unnecessarily restrictive.

Example:

```text
Design A

Cycle:  1 2 3 4 5 6
valid:  0 1 1 0 1 0
retry:  0 1 0 0 0 0
data:   X A A X B X
```

```text
Design B

Cycle:  1 2 3 4 5 6
valid:  0 0 1 1 0 1
retry:  0 0 1 0 0 0
data:   X X A A X B
```

Both represent the same transaction stream:

```text
A
B
```

The verifier should consider these designs equivalent.

---

# Semantic Definition

A transaction is **accepted** when:

```text
accepted := valid && !retry
```

If

```text
retry == 1
```

the receiver did **not** consume the value.

The sender must continue presenting the same transaction until it is eventually accepted.

Therefore:

```text
retry == 1

    implies

valid remains asserted
and
data remains unchanged
```

Only accepted transactions participate in equivalence.

The equivalence relation becomes:

```text
sequence(data when acceptedA)
==
sequence(data when acceptedB)
```

Cycle alignment is intentionally ignored.

---

# Shared Stream Transformer

The central abstraction is a **Shared Stream Transformer (SST)**.

Instead of verifying two equivalent elastic regions independently:

```text
input
   |
+------+      +------+
|  A   |      |  B   |
+------+      +------+
   |              |
outputA       outputB
```

collapse them into:

```text
                    +--------------------+
shared input ------>| Shared Stream      |
                    | Transformer (SST)  |
                    +--------------------+
                               |
                      shared output stream
```

Conceptually:

```text
output_stream = F(input_stream)
```

where `F` operates on accepted transactions rather than cycles.

Latency is intentionally abstracted away.

---

# Stream Semantics

The SST consumes:

```text
(valid, retry, data)
```

but semantically advances only on:

```text
accepted = valid && !retry
```

Repeated cycles while `retry == 1` are interpreted as the same pending transaction rather than multiple transactions.

Conceptually:

```text
valid retry data

1     1     A
1     1     A
1     1     A
1     0     A
```

corresponds to exactly one transaction:

```text
A
```

---

# BMC Perspective

Traditional BMC:

```text
outA[t] == outB[t]
```

Elastic BMC:

```text
nth_accepted_transaction(outA)
==
nth_accepted_transaction(outB)
```

Latency differences and retry stalls are ignored.

Only transaction ordering and values matter.

---

# Pono Encoding

The SST can be modeled as shared symbolic state.

Conceptually:

```text
state

    S

transition

    if accepted:
        S' = T(S, data)

    else:
        S' = S

output

    output_transaction = O(S)
```

Both designs connect to the same symbolic object.

No duplicated internal elastic state is introduced.

---

# Lightweight Alternative

An even cheaper abstraction is a shared nondeterministic accepted transaction stream.

Introduce shared symbolic outputs:

```text
shared_valid
shared_retry
shared_data
```

with protocol constraints:

- accepted transactions are deterministic
- retry does not create new transactions
- retry preserves the current transaction
- ordering preserved
- no duplication
- no loss

The internal implementation remains completely abstract.

---

# Elastic Contract

Every collapsed elastic region should expose:

```text
ElasticContract {

    input_ports

    output_ports

    valid semantics

    retry semantics

    ordering

    reset semantics

    flush semantics

    optional bounded buffering

    optional bounded latency

}
```

Only compatible contracts may be collapsed.

---

# Stream-Level Equivalence Cache

Maintain a persistent cache independent of cycle-level equivalence.

Suggested signature:

```text
ElasticSignature {

    interface_hash

    protocol_hash

    retry_semantics_hash

    ordering_hash

    reset_hash

    flush_hash

    latency_class

    assumption_hash

}
```

Successful proofs become reusable across optimization passes.

---

# Verification Strategy

Instead of:

```text
whole design

      |

    global BMC
```

perform:

```text
whole design

      |

identify elastic regions

      |

prove accepted input streams equal

      |

collapse to Shared Stream Transformer

      |

perform reduced BMC
```

Verification complexity should depend primarily on the non-collapsed logic.

---

# Long-Term Research Direction

A promising extension is to make elasticity a first-class semantic property of the IR.

Rather than treating `valid` and `retry` as ordinary wires, LiveHD could explicitly represent:

```text
TransactionStream<T>
```

with semantics:

- accepted transactions
- retry-preserved values
- ordering
- optional buffering
- optional latency bounds

Verification would then naturally become:

```text
same accepted transaction stream
```

instead of

```text
same waveform
```

allowing retiming, elastic buffering, pipeline restructuring, and latency-changing optimizations while preserving transaction-level correctness.
