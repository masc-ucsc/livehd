# LiveHD Relational Equivalence Engine (`pass/lec`)

> **Reference — how and why `pass/lec` works today.** Current work and the
> longer roadmap live in [`todo/livehd/2f-lec.html`](../../todo/livehd/2f-lec.html)
> (LEC remainder) and [`todo/livehd/2f-fcore.html`](../../todo/livehd/2f-fcore.html)
> (the shared formal core: proof-DAG runner, verdict cache, case-split, hints). The
> original Pono+smt-switch implementation plan and the fluid-pipeline research
> appendix were retired (superseded by the cvc5-direct architecture below); their
> full text is in git history if needed.

`pass/lec` is **not** just a LEC tool — it is a **relational query engine over
LiveHD `LGraph`s** that *happens* to back `lhd lec`. The question it answers is
*"what is the relationship between these two subsets of logic (in one design, or
across two)?"*:

- `prove_equal(a, b)` — are cones `a` and `b` equal for all reachable states & inputs?
- `prove_distinct(a, b)` — are they *guaranteed never* equal?
- `is_sat(predicate)` — is some relation reachable at all? (returns a witness)

Two clients motivate it: **(1) equivalence checking** — `prove_equal(impl.outputs,
ref.outputs)` under `assume_equal(inputs)`, which backs `lhd lec`; and **(2)
design queries for other passes** — e.g. a memory-port optimizer asking
`prove_equal(portA.addr, portB.addr)` to merge ports, or `prove_distinct(write.addr,
read.addr)` to license a no-forwarding assumption. Same engine, same encoder,
different query.

## Architecture: cvc5-direct

The discharge stack is **cvc5 talked to directly via its native C++ API** — no
Pono, no smt-switch. A combinational `prove_equal` is just a miter `bad =
OR(ref_i != impl_i)` discharged with **one `cvc5::Solver::checkSat`** (UNSAT =
equal, SAT = witness). Sequential proofs reuse the same encoder over an unrolled
or inductive frame (below). cvc5 covers `QF_ABV` (bit-vectors + arrays), which is
everything the encoder needs.

```
   L3  Orchestration     pass.lec  /  lhd lec  /  pass-facing query helpers
        v
   L1  Query API         prove_equal / prove_distinct / is_sat  (query.{hpp,cpp})
        |  builds the miter, picks a frame (comb / inductive / BMC), one+ checkSat
        v
   L0  Encoder           LGraph -> cvc5 bit-vector / array terms  (encode.{hpp,cpp})
        |  comb node -> Term ; flop -> cut symbol + next-state ; memory -> array ;
        |  module IO -> input/term ; comb Sub -> inlined ; opaque Sub -> blackbox
        v
       cvc5 (QF_ABV, native C++ API)  +  hermetic @gmp
```

- **L0 Encoder** (`encode.cpp`, ~1.1k lines) is the big reusable piece and is
  deterministic + side-effect free: it is "`cgen` that emits cvc5 terms instead
  of Verilog" — same walk order, same per-op switch as `inou/cgen`.
- **L1 Query API** (`query.cpp`) assembles the relation and chooses the frame.
- **L3** is thin CLI/pass glue mirroring `pass.color` / `pass.partition` / the
  `lhd lec` plumbing.

## How a proof runs

- **Combinational** (default for stateless cones): encode both designs sharing
  the primary-input symbols, miter the outputs, one `checkSat`.
- **Sequential — inductive flop-cut miter** (`lec.engine=ind`):
  flops are **cut points** matched across designs by preserved name
  (`flop_state_key()` normalizes Yosys `$…$` decorations + SSA suffixes → a
  stable 1:1 state map). Assume the mapped current-state equal, prove next-state
  and outputs agree — a combinational base+step, definitive `Proven` without
  unrolling. False-refutes only on **unreachable** states where two front-ends
  resolve a don't-care differently.
- **Sequential — BMC from reset** (`lec.engine=bmc`): start from the reset state
  (each flop's constant `initial`, or a shared fresh symbol for a reset-less
  flop) and chain each design's own next-state forward `bound` cycles, so only
  **reachable** states are checked. Reset-phase separation (`lec.phase`):
  `after_reset` (default; hold reset `reset_cycles` cycles to drive both into
  reset state, then deassert and miter the following cycles — free-running
  agreement), `just_reset` (hold every primary reset asserted each cycle and
  miter — agreement during reset), `free_toreset` (resets range freely; the
  solver may still assert them, exploring odd reset patterns), `full` (require
  agreement in BOTH `just_reset` and `after_reset`). Primary resets are auto-detected (inputs that drive a
  flop `reset_pin`, plus canonical `rst`/`reset`/`*_n` names with polarity
  inferred) or given explicitly via `lec.reset`.
- **auto — parallel portfolio** (`lec.engine=auto`): race the inductive miter and
  the BMC-from-reset engine as **two forked worker processes** (cvc5 cross-instance
  thread-safety is unverified, so separate processes — free kill of the loser),
  and take the first **trustworthy** verdict. The pairing is complementary
  (inductive *proves* deep state; BMC finds *shallow real bugs*); the content is
  the **verdict-trust asymmetry**: inductive **Proven** (UNSAT + complete
  correspondence) ⇒ PASS, kill BMC; BMC **Refuted** (reachable CEX from reset) ⇒
  FAIL + witness, kill inductive; inductive **Refuted** ⇒ a *hint only* (single-step
  assumes an arbitrary equal state, so the CEX may be an unreachable step-case — it
  must never hard-fail the build); BMC bounded-**Proven** (no CEX ≤ bound) ⇒
  accepted as PASS by the bounded-proof policy (`try_bounded_proven`), disclosed
  as bounded — cycles beyond the bound stay unproven, and the bound is part of the
  verdict-cache key; neither trustworthy ⇒ **inconclusive**. The per-query `lec.timeout`
  bounds each worker, so a hard miter self-limits and the portfolio degrades to
  inconclusive rather than hanging.
- **Memory**: `Memory` cells → SMT **theory of arrays**. Corresponding memories
  (matched by signature + `forward_class()` occurrence via `mem_state_key()`)
  collapse to **one shared array symbol**; `dout = select(array, addr)`,
  next-state `array' = store(array, addr, din)` applied in port order. Read ports
  need no explicit cross-design pairing — `select` at a shared array + equal
  addresses makes douts correspond via the compared (name-matched) outputs; only
  same-cycle same-address WRITE collisions are port-order-sensitive. A `type==2`
  array (runtime-indexed comb array / ROM) has **no cross-cycle persistence**: its
  base contents are rebuilt each cycle from the whole-array `update` bus or the
  comptime `init` constant — built **per design** (not pinned onto the shared
  symbol, which would be a vacuous proof when the two inits differ), so equal
  inits prove and differing inits refute.
- **Hierarchy**: a **combinational** `Sub` whose def is supplied via `lhd lec
  --lib lg:DIR` is **flattened inline** (def encoded with inputs bound to the
  instance's input Vals, outputs wired onto its output pins) — the prime use is
  an ABC standard-cell netlist whose blackbox cells resolve to `pass.liberty
  gensim` models. Unresolved / stateful / too-deep `Sub`s become a sound
  **blackbox** (inputs = compare points, outputs = free symbols shared across the
  designs by the box-correspondence key below).
- **Box correspondence** (query.cpp builder, `Encoder::set_box_keys`): every Sub
  the encoder will treat as a box is enumerated ONCE per design (the encoder's
  own hierarchical walk + blackbox predicate) and keyed `defname#tag`. `tag` is
  the **canonicalized instance hier-NAME** when that name occurs exactly once on
  BOTH sides (a Verilog instance name and a Pyrope `::[name=]` attr both land on
  the Sub node), else an occurrence index over the unnamed/unmatched remainder.
  The encoder looks keys up (a miss is a loud encode error, never a silent
  re-count) — this retired the old per-walk occurrence counters, whose drift
  between the encoder and the query-side builders could silently degrade a
  stateful box to a stateless constant one (a false-PASS hazard) and whose
  traversal-order pairing falsely refuted reversed interchangeable instances
  (tests/equiv/instance_collapse_order). A key present on ONE side only yields
  one-sided obligations, which BOTH engines gate to an incomplete
  correspondence: no Proven with unchecked obligations, no Refuted through an
  unjustified shared-box assumption — only INCONCLUSIVE.
- **Proven-module collapse** (`lhd lec --collapse <def>` / `lec.collapse`): a def
  the driver has already proven equivalent is **forced** to the blackbox path
  even when it could be flattened (so the parent stops re-solving its internals).
  A *stateless* leaf is **pairing-free** (`Comb_box`): each output is
  `UF_def_port(inputs)` with ONE uninterpreted function per (def, port) shared
  across both designs and ALL instances, so congruence pairs interchangeable
  instances dynamically — no correspondence key exists to get wrong, no bbin
  obligations are emitted, and instance-count mismatches cost nothing. The
  outputs are still emitted as per-instance fresh symbols on the first visit and
  tied to the UF once the inputs resolve (`Encoded::equalities`), so an
  output-feeds-own-input false cycle cannot deadlock the fixpoint. A **stateful**
  leaf becomes a **state-aware** box: `outputs = UF(state)` (Moore),
  `next_state = UF2(inputs, state)` over one shared state cut per corresponding
  instance pair (name-first pairing above; matched-reset shared init, threaded by
  the BMC unroll), so the leaf output varies per cycle — a constant combinational
  box would false-prove a timing difference through a stateful leaf. Both box
  kinds build their UF input concat from a **NAME-SORTED, cross-design-unioned
  port layout** (`in_ports`) — never a side's own decl order, which front-ends
  permute (slang keeps the .v port list, Pyrope appends implicit clock/reset) and
  a permuted concat feeds the shared UF different values for equal inputs. The
  Moore output is emitted *before* the inputs are resolved, so a collapsed stage
  register whose output feeds glue back to its own stall/enable input does not
  close a false combinational cycle; divergent leaf inputs are still caught by
  the compare points. Collapsing a same-library submodule (one `forward_hier`
  would descend into + the edge resolver would thread through) uses the hhds
  `Hier_opaque_scope` so the leaf is opaque to BOTH the walk and the
  cross-boundary edge resolution.
- **Bottom-up hierarchical** (`lec.hierarchical=true`): build the module-def
  dependency DAG over the defs present BY NAME in both libraries, topo-order it
  **leaves-first**, and LEC each def under the `auto` portfolio. Record the proven
  set; for each parent, force-black-box its **proven** child instances (collapse,
  above) so the parent proof stops re-solving them. A child **not** provable in
  isolation with free inputs (context-dependent equivalence) is **not** collapsed
  — it stays flattened into the parent and is proven in context (the M5 CEGAR /
  un-black-box fallback). Correspondence is name-based, so no semdiff is needed
  when the call structures match. A parent **REFUTED under a non-empty collapse
  set is never final**: the collapse over-approximates the children (free/UF box
  values the real leaf may never emit; occurrence-paired unnamed instances may
  mispair), so the driver re-solves the def FLAT before reporting — flat-Proven
  is adopted, flat-Unknown stays inconclusive, and a FAIL is only ever reported
  from a counterexample free of collapse boxes (true blackboxes for unresolved
  defs may remain; they correspond explicitly and gate to inconclusive when
  one-sided). The same confirmation guards a manual `--collapse` on the
  non-hierarchical path. (tests/equiv/instance_state_anon +
  lhd/tests/lec_box_pairing_test pin this.) Each def emits a per-block progress
  line the instant it resolves; the TOP def's verdict is the result. (v1 walks
  the DAG sequentially; proving independent leaves in parallel is a later
  speedup.)
- **Structural def-diff skip** (`lec.semdiff=structural`, M3): in the bottom-up
  flow, run `pass/semdiff::structural_match` per module BEFORE the solver. A def
  whose ref/impl are **structurally identical** (no unmatched node on either side,
  flops anchored by name) **and whose children are all already proven** is dropped
  as proven with **no solver call** — only the *changed* defs reach cvc5 (a
  1000-def design with a 1-def edit checks ≈1 def). The children-proven guard
  keeps it sound: a parent's own-structure match does not cover a child's internal
  diff, so a non-equivalent child is never masked (it fails to match, is solved,
  refutes, and blocks the parent's skip). Cross-front-end pairs (slang vs pyrope)
  are usually *functionally* equivalent but *structurally* different, so semdiff
  skips nothing there and every def is solved — it pays off on re-builds of the
  same source.

## Encoder reference (L0) — LiveHD graph facts

The post-elaboration netlist is an `hhds::Graph`; the op enum and conveniences
live in `graph/cell.hpp` + `graph/node_util.hpp`. Reuse `graph_util::*` verbatim
— do **not** re-derive pin/bit/sign conventions.

**Walk** (mirror `cgen_verilog.cpp::do_from_graph`): module IO from
`Graph::get_io()` (`DeclaredIoPin{name,port_id,bits,unsign}`) → combinational
nodes in topo order via `Graph::forward_class()` → `Memory`/`Sub`/`Flop` via
`forward_class()` filtered by `graph_util::type_op_of(node)`. The
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
*regardless* of the sign flag. The real bit-vector width is
`w = graph_util::is_unsign(pin) ? bits_of(pin) - 1 : bits_of(pin)` — a 4-bit
unsigned bus is stored as `bits=5` (`encode.cpp::real_width`). `is_unsign(pin)` ≡
the `pin_signed` attr is absent. Off-by-one here silently breaks every
comparison/arith equivalence. (cgen proves it: `reg signed [bits-1:0]` for
signed, `bits-1` for unsigned.) Exception: under `--lib`, a mapped netlist's
unsigned nets carry no spare sign bit, so the encoder uses the **raw**
`bits_of` width there.

**Op-semantics traps** (build the table from the real `Ntype_op`s, per
`process_simple_node`): `Sum` — a-drivers add, **b-drivers subtract**. `SRA` —
arithmetic (sign-preserving); there is **no** logical-rshift op. `SHL` — the `b`
amount is **one-hot multi-driver**: `(v<<b0)|(v<<b1)|…`. `Get_mask(a,-1)` =
zero-extend; contiguous mask = part-select. `Mux` — `sel` pid0, arm `i` chosen
when `sel==i-1`; `Hotmux` — one-hot `sel`. `LT/GT` can be multi-input (all
a-vs-b pairs ANDed). **Decomposed at tolg, so absent as primitives:** `Mod`,
reduce-AND/XOR, popcount, `GE/LE/NE`, logical-rshift — they appear as
`Ror/Not/And/Get_mask/SRA/EQ` compositions. `AttrSet` = no-op pass-through.

**Flop (`Ntype_op::Flop`)** → one cut symbol / state var. Pins: `0 async, 1
initial (reset value), 2 clock_pin, 3 din, 4 enable, 5 negreset, 6 posclk, 7
reset_pin, 8 pipe_min, 9 pipe_max`. `next = enable ? din : q` (enable false =
hold); `reset_pin` active → `q' = initial` (default 0); `pipe_min`>1 models an
N-deep shift register (N state vars). Output `q = driver_pin(0)`.

**Memory (`Ntype_op::Memory`)** → SMT theory of arrays. **16-pin port stride**
(`Ntype::Memory_port_stride`): port *k*'s pins are at `pid + 16*k`
(`get_sink_name` does `pid % 16`); per-port logical pins `0 addr, 2 clock_pin,
3 din, 4 enable, 10 rdport (1=read/0=write)`; comptime / cell-global pins (block
0 only) `1 bits, 5 fwd, 6 posclk, 7 type (0 async-rd / 1 sync-rd / 2 array-ROM),
8 wensize, 9 size, 11 init, 12 update, 13 update_enable, 14 reset` (15 reserved).
Read-data output for read port *N* = `driver_pin(wr_ports + N)`; the whole-array
async read uses the reserved driver pid `Memory_readall_pid`.

## `lhd lec` CLI & options

`lhd lec` lives in `lhd_kernel.cpp::lec_command` and discharges via
`lec::prove_equal` directly (clean `equiv_fail` error + witness). Sides take
`--impl KIND:PATH` / `--ref KIND:PATH` (`verilog:`/`lg:`/`pyrope:`/`ln:` or a
bare path; verilog elaborates through `--reader`, default slang). The top is
picked per side via `--impl-top` / `--ref-top`, falling back to `--top`, falling
back to the sole module. `--lib lg:DIR` (repeatable) supplies Sub def graphs for
inline flattening.

```
lhd lec --impl impl.prp --ref ref.v
lhd lec --impl lg:impl/ --ref lg:ref/ --top foo --set lec.engine=bmc --set lec.phase=after_reset
lhd lec --impl net.v --ref gold.v --set lec.solver=lgyosys --top foo   # yosys/lgcheck backend
```

The `pass.lec` Eprp method's `add_label_optional(...)` calls **are** the `lec.*`
switch registry (`lhd list options 'lec\..*'`, `lhd describe lec.<flag>` derive
from them); `{"lec","pass.lec"}` in `kSetPasses[]` makes typos hard-error (never
a silent no-op).

| Flag | Meaning | Default |
|---|---|---|
| `lec.engine` | discharge frame: `auto` (parallel portfolio) \| `bmc` \| `ind` (inductive flop-cut) \| `ic3` | `auto` |
| `lec.solver` | backend: `cvc5` (in-process) \| `bitwuzla` (opt-in, may be unbuilt) \| `lgyosys` (yosys/lgcheck) | `cvc5` |
| `lec.bound` | BMC / induction depth bound `k` | `6` |
| `lec.timeout` | per-query wall-clock seconds (`0` = none) | `0` |
| `lec.witness` | print the counterexample on `Refuted` (and gate the lecfail testbench below) | `true` |
| `lec.prpfail` | `lhd lec` + `--workdir` only: on `Refuted`, write a self-contained Pyrope testbench driving BOTH designs with the counterexample sequence. A filename under `--workdir`; `true`=`lecfail.prp`; `""`/`false`=off. Default `lecfail.prp` iff `--workdir` set | `lecfail.prp` (else `""`) |
| `lec.prpfailrun` | run the generated `lec.prpfail` testbench through `lhd sim --set sim.vcd=true` to dump the waveform (same basename, `.vcd`) | `true` iff `--workdir` set |
| `lec.phase` | BMC reset phase: `after_reset` \| `just_reset` \| `free_toreset` \| `full` | `after_reset` |
| `lec.reset_cycles` | `after_reset` phase: reset-hold prologue length | `2` |
| `lec.reset` | explicit reset inputs `name[:lo\|:hi]`, comma-sep (else auto-detect) | `""` |
| `lec.collapse` | proven-module collapse: comma-sep def names forced to the sound blackbox | `""` |
| `lec.hierarchical` | bottom-up: LEC every def leaves-first under `lec.engine`, collapsing proven children (`false` = flat single LEC) | `true` |
| `lec.semdiff` | structural def-diff skip: `structural` (drop a structurally-identical def with no solver; `true`/`on` alias) \| `none`. NB cross-front-end pairs never match | `structural` |
| `lec.decompose` | split the miter into per-cut queries: `auto` (sweep, fall back to the monolithic solve on a non-discharging cut) \| `true` (sweep only, report the hard residue, no monolithic solve) \| `false` (monolithic only) | `auto` |
| `lec.cross` | also run `lgcheck` and assert agreement (bring-up only) | `false` |

The `lgyosys` solver shells out to `inou/yosys/lgcheck` (yosys `equiv`, the
former `lhd check`) — the only backend that reads Verilog without a front-end
reader, kept as a **bring-up cross-check oracle** and the path for gate-level
netlists; the goal is to *replace* it (it does not scale past combinational
logic). It is **never** on the production trust path.

## The lecfail witness testbench (`lhd lec --workdir DIR`)

When `--workdir` is set, a **REFUTED** verdict does more than print a
counterexample string: it drops a runnable, self-contained Pyrope reproduction
into the workdir. `lec.prpfail` (default `lecfail.prp`) is a single file holding

- **both designs** re-emitted as Pyrope (`lhd compile --emit-dir pyrope:`; the
  ref side's modules are prefixed `lecref_` on a name clash),
- a wrapper `mod __lecfail_dut_pair` that instantiates BOTH and exposes their
  outputs as `impl_<o>` / `ref_<o>`, and
- a `test` that drives the BMC counterexample's exact per-cycle input sequence
  (reset-hold prologue included) via `const _drv_<in> = [...]` arrays.

`lec.prpfailrun` (default on with `--workdir`) then runs `lhd sim
--set sim.vcd=true` on it, producing **one** VCD (`lecfail.vcd`) whose
`impl_*` vs `ref_*` traces show exactly where and how the two designs diverge —
open it in a waveform viewer, or edit the `.prp` and re-run. The trace is the
uncapped structured witness the BMC engine records (`Query_result::trace`); the
inductive engine's single-step CEX is not a reachable-from-reset sequence, so it
produces no testbench (a plain `auto`/`bmc` REFUTE always does). Turn the whole
thing off with `lec.witness=false` (or `lec.prpfail=""`); keep only the `.prp`
with `lec.prpfailrun=false`. Regression: `pass/lec/tests/lec_witness_prpfail_test.sh`.

## Build facts (cvc5)

- **cvc5** is vendored as the official **prebuilt non-GPL static** lib
  (`packages/cvc5.BUILD`, pin `cvc5-1.3.4`), `select()`-ed per host: Linux
  x86_64 / Linux arm64 / macOS arm64 — **no from-source solver build**. The
  prebuilt `libcvc5.a` + `libcadical.a` + LibPoly are merged into **one
  relocatable object** (`ld -r --whole-archive`) and the **CaDiCaL symbols are
  localized** (`objcopy --localize-symbol '*CaDiCaL*'`) because **Berkeley-abc**
  (linked into the same `lhd` binary via Yosys) **also vendors CaDiCaL** →
  duplicate strong symbols at the final link. `libcvc5parser.a` is dropped (the
  C++-API backend never uses the SMT-LIB parser). NB: `ar x` flattens duplicate
  member basenames — must use `ld -r --whole-archive`, never extract.
- **GMP** is the hermetic `@gmp` BCR module (no system/homebrew libgmp). It is
  LGPLv3 — keep it linkable per the LGPL relink obligation; list it in `NOTICE`.
- The `@cvc5` target is the **only** SMT dependency in `pass/lec/BUILD`
  (`lec_query` deps `//graph`, `@cvc5`, `@hhds//hhds:graph`). MathSAT /
  interpolation engines are excluded (non-redistributable).

## Tests

`bazel test //pass/lec/...`: `cvc5_link_smoke` (link + a `QF_ABV` checkSat),
`query_test` (encode + prove_equal on tiny comb graphs), `lec_cross_test`
(verdicts agree with `lgcheck`), `lec_phase_test` (BMC reset phases),
`lec_mem_test` / `lec_arrfield_test` / `lec_combarray_test` (the array engine on
compiled RTL). The strongest encoder check: emit *both* the SMT model and the
cgen Verilog from the **same** graph and LEC them via `lgcheck` — any
disagreement is an encoder bug caught before it can mask a real design diff.

## References

- **cvc5** C++ API (`TermManager`, scoped `Kind`, `mkBitVector`/`mkArraySort`,
  `Solver::checkSat`) · github.com/cvc5/cvc5 (modified BSD-3, `--no-gpl`)
- **LiveHD encoder surface** — `graph/cell.hpp` (`Ntype_op`),
  `graph/node_util.hpp` (`graph_util::*`), `inou/cgen/cgen_verilog.cpp`
  (reference walker / `process_simple_node`), `inou/yosys/lgyosys_tolg.cpp`
  (Flop/Memory/Sub builder), `pass/bitwidth/bitwidth.cpp` (`bits` = magnitude+1)
- **SEC methodology** — van Eijk register correspondence (TCAD'00); Cheng SEC
  (HLDVT'05); ABC `dsec`/`dprove` (FRAIG SAT-sweeping)
