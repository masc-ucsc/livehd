# LiveHD Relational Equivalence Engine (`pass/lec`)

> **Reference — how and why `pass/lec` works today.** Current work and the
> longer roadmap live in [`todo/livehd/lec.html`](../../todo/livehd/lec.html). The
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
- **Sequential — inductive flop-cut miter** (`lec.engine=ind`, the default):
  flops are **cut points** matched across designs by preserved name
  (`flop_state_key()` normalizes Yosys `$…$` decorations + SSA suffixes → a
  stable 1:1 state map). Assume the mapped current-state equal, prove next-state
  and outputs agree — a combinational base+step, definitive `Proven` without
  unrolling. False-refutes only on **unreachable** states where two front-ends
  resolve a don't-care differently.
- **Sequential — BMC from reset** (`lec.engine=bmc`): start from the reset state
  (each flop's constant `initial`, or a shared fresh symbol for a reset-less
  flop) and chain each design's own next-state forward `bound` cycles, so only
  **reachable** states are checked. Reset-phase separation (`lec.phase`): `free`
  (resets range freely), `reset` (hold every primary reset asserted each cycle
  and miter — agreement under reset), `run` (hold reset `reset_cycles` cycles to
  drive both into reset state, then deassert and miter the following cycles —
  free-running agreement). Primary resets are auto-detected (inputs that drive a
  flop `reset_pin`, plus canonical `rst`/`reset`/`*_n` names with polarity
  inferred) or given explicitly via `lec.reset`.
- **Memory**: `Memory` cells → SMT **theory of arrays**. Corresponding memories
  (matched by signature + `forward_class()` occurrence via `mem_state_key()`)
  collapse to **one shared array symbol**; `dout = select(array, addr)`,
  next-state `array' = store(array, addr, din)` applied in port order.
- **Hierarchy**: a **combinational** `Sub` whose def is supplied via `lhd lec
  --lib lg:DIR` is **flattened inline** (def encoded with inputs bound to the
  instance's input Vals, outputs wired onto its output pins) — the prime use is
  an ABC standard-cell netlist whose blackbox cells resolve to `pass.liberty
  gensim` models. Unresolved / stateful / too-deep `Sub`s become a sound
  **blackbox** (inputs = compare points, outputs = free symbols, keyed by
  def-name + occurrence so both sides line up).

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

**Memory (`Ntype_op::Memory`)** → SMT theory of arrays. **12-pin port stride**:
port *k*'s pins are at `pid + 12*k` (`get_sink_name` does `pid % 12`); per-port
logical pins `0 addr, 2 clock_pin, 3 din, 4 enable, 10 rdport (1=read/0=write)`;
comptime pins `1 bits, 5 fwd, 6 posclk, 7 type (0 async-rd / 1 sync-rd / 2
array-ROM), 8 wensize, 9 size, 11 init`. Read-data output for read port *N* =
`driver_pin(wr_ports + N)`.

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
lhd lec --impl lg:impl/ --ref lg:ref/ --top foo --set lec.engine=bmc --set lec.phase=run
lhd lec --impl net.v --ref gold.v --set lec.solver=lgyosys --top foo   # yosys/lgcheck backend
```

The `pass.lec` Eprp method's `add_label_optional(...)` calls **are** the `lec.*`
switch registry (`lhd list options 'lec\..*'`, `lhd describe lec.<flag>` derive
from them); `{"lec","pass.lec"}` in `kSetPasses[]` makes typos hard-error (never
a silent no-op).

| Flag | Meaning | Default |
|---|---|---|
| `lec.engine` | discharge frame: `bmc` \| `ind` (inductive flop-cut) \| `ic3` | `ind` |
| `lec.solver` | backend: `cvc5` (in-process) \| `bitwuzla` (opt-in, may be unbuilt) \| `lgyosys` (yosys/lgcheck) | `cvc5` |
| `lec.bound` | BMC / induction depth bound `k` | `20` |
| `lec.timeout` | per-query wall-clock seconds (`0` = none) | `0` |
| `lec.witness` | print the counterexample on `Refuted` | `true` |
| `lec.phase` | BMC reset phase: `free` \| `reset` \| `run` | `free` |
| `lec.reset_cycles` | `run` phase: reset-hold prologue length | `2` |
| `lec.reset` | explicit reset inputs `name[:lo\|:hi]`, comma-sep (else auto-detect) | `""` |
| `lec.cross` | also run `lgcheck` and assert agreement (bring-up only) | `false` |

The `lgyosys` solver shells out to `inou/yosys/lgcheck` (yosys `equiv`, the
former `lhd check`) — the only backend that reads Verilog without a front-end
reader, kept as a **bring-up cross-check oracle** and the path for gate-level
netlists; the goal is to *replace* it (it does not scale past combinational
logic). It is **never** on the production trust path.

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

`bazel test //pass/lec/...`: `cvc5_link_test` (link + a `QF_ABV` checkSat),
`comb_equiv_test` (encode + prove_equal on tiny comb graphs), `lec_cross_test`
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
