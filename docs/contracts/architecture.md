# LiveHD Architecture — IRs, partitions, and the agent loop

This document is the cross-cutting design overview that the other contracts
(`lnast_spec.md`, `lnast2lgraph.md`, `attributes_spec.md`, `simulation.md`,
`hhds_migration.md`, `future_cli.md`) plug into. It is the umbrella; each
contract files in the per-pass / per-IR details.

## 1. North star

LiveHD is the compiler substrate for agent-driven chip-design loops. The
optimisation target is wall-clock iteration time on a single design edit,
not throughput on a million-cell netlist. Two loops dominate:

- **Feature / debug loop** — edit Pyrope, simulate, observe, iterate.
  Seconds matter; cold restart of a multi-minute simulation kills the loop.
- **Optimisation loop** — edit Pyrope or a synth knob, measure timing /
  area / power on the affected partition, iterate. Same time budget.

Everything below is justified by one of these two loops.

## 2. Two IRs, three lowerings

```
                            ┌─→ hlop / slop kernels  → fast simulation
                            │                          (hot-reloadable)
LNAST (HHDS-backed)         │
  ├─→ upass (shared trunk) ─┤
  │   constprop, attributes,│
  │   SSA, partition decl,  │
  │   tuple/loop expansion  │
  │                         │
  └─ source-map indirection ┴─→ LGraph                → synthesis,
                                                        Verilog egress,
                                                        LEC,
                                                        large simulation
```

- **LNAST is the evaluation IR.** Conditional / lazy execution is
  preserved: a comb function inside an `if` does not run every cycle.
  This is the productivity multiplier for fast simulation; it is lost
  the moment a design is flattened to Verilog modules.
- **LGraph is the synthesis IR.** Cells are materialised, partitions
  are realised as subgraphs, pin layouts are fixed. Used by every step
  whose output is "real hardware" — synth, LEC, Verilog egress, and
  the large / cycle-accurate simulation backend.
- **hlop/slop is the fast-sim codegen.** `slop` generates native
  kernels directly from LNAST; `dlop` is the dynamic/traversal-friendly
  counterpart. Skips LGraph entirely on the inner loop. See
  `/Users/renau/projs/hlop/implementation_plan.md`.
- **upass is the shared trunk.** Both backends consume the same
  post-upass LNAST. New passes belong here, not in either backend.

A consequence: `lnast2lgraph` is the *synthesis-path* lowering. It is
important but not the agent loop's hot path. `lnast2hlop` (slop codegen)
is the *simulation-path* lowering and owns the inner-loop latency budget.

## 3. Partitions are first-class

Every `hhds::Tree` in the Forest carries a `Partition` descriptor.
Partitions are the unit at which incrementality, LEC, synth feedback,
cross-clock checks, and hot-reload all key off.

```cpp
struct Partition {
  enum class Kind { comb, pipe, mod };           // flow deferred
  Kind     kind;
  uint16_t latency;          // pipe: N. comb: 0. mod: variable.
  Latency_range latency_range; // pipe[1..<N] form; tool may pick any
                               // value in the closed range. Equal to
                               // {N,N} for plain pipe[N]. Summary of the
                               // per-output Port::stages (1q) — for pipe
                               // all outputs agree; mod may differ
                               // per output.
  Clock_id clock;            // partition-level clock domain
  Reset_id reset;            // partition-level reset
  std::vector<Port> inputs;
  std::vector<Port> outputs;
  std::vector<Ext>  ext;     // memory / submodule interfaces
  uint64_t interface_hash;   // hash(kind, latency_range, sorted ports, ext)
  uint64_t state_shape_hash; // hash(internal reg/memory decls). comb=0.
  Loc      decl_loc;         // source anchor — see §6
};

struct Port {
  std::string_view name;     // globally unique under partition path
  Type             type;
  std::optional<uint16_t> bits;
  bool             is_signed;
  Loc              decl_loc;
  std::optional<Role> role;  // clock | reset | valid | ready | data
  Latency_range    stages;   // per-OUTPUT pipe stages {min,max} (1q):
                             // pipe[N]={N,N}, pipe[2..=5]={2,5}, bare
                             // pipe={1,0} (max 0 = unconstrained),
                             // comb={0,0}; a mod output may carry a
                             // different value per output. Semantics:
                             // SCC/σ depth from inputs to that output
                             // flop (06c-pipelining.md). Inputs: {0,0}.
};
```

### 3.1 Kinds and contracts

| Kind | Statefulness | Latency contract | LEC strategy |
|---|---|---|---|
| `comb` | None. Pure function. | 0 | Direct combinational equivalence |
| `pipe[N]` | N stages of pipeline regs; no internal feedback | Exact N | Symbolic time-shift then combinational |
| `pipe[1..<N]` | Same shape; tool may pick latency in `[1, N)` | Closed range; hard contract | Latency-insensitive equivalence under retiming |
| `mod` | General; may contain memories, cross-clock, submodules | Variable | Partition further; otherwise treat as opaque |

`pipe[N]` is a **hard contract**: Pyrope refuses to compile a body that
cannot be retimed to N cycles. `pipe[1..<N]` is also hard — the body
must support the **entire** declared range (its intrinsic stage depth σ
must be ≤ the range minimum, task 1q), so a caller may rely on any value
in the range without knowing the body; the tool picks.

### 3.2 I/O is a flat, named, typed port list

Each port has its own bits, sign, role, and `decl_loc`. Tuple types
inside the partition body still compose (`out = (sum, carry)`); the
partition boundary destructures. This keeps `pass/bitwidth` local to a
port — bit-width inference never has to walk a tuple type across
statement boundaries.

LGraph mapping is 1-to-1: input port → graph input pin, output port →
graph output pin. The 1-based pin numbering from `lnast2lgraph.md §5`
becomes the declared port order.

### 3.3 External boundaries

`ext:` declares memories and submodule references as named external
partitions. This is the syntactic anchor LEC uses for memory abstraction
(§5.2). A `mod` with no `ext:` is allowed; the LEC pass will try to
infer the same boundary heuristically.

## 4. Incrementality

The unit of incrementality is the LNAST tree. upass results are cached
per-tree, content-addressed by `(tree_body_hash, deps' interface_hash)`.

- **Cache key**: tree body content hash + sorted hashes of every
  dependency's `interface_hash`. Edit a function → that tree's body
  hash changes. If its `interface_hash` did not change, no caller
  invalidates.
- **Global pass becomes demand-driven.** The "global LNAST upass" of
  `lnast2lgraph.md §1` runs only for callers of trees whose
  `interface_hash` changed. This replaces the eager whole-forest
  traversal.
- **HHDS Phase 7 is the substrate.** `Tree::clone()`,
  `Forest::create_tree_temp()`, and `TreeIO::replace()` (see
  `hhds_migration.md §7`, `/Users/renau/projs/hhds/hhds_todo.md`) are
  what makes per-tree cache replacement atomic.
- **Parallelism falls out.** Trees with no edge between them in the
  call graph can be re-upassed in parallel; the demand-driven global
  pass orchestrates the join.
- **Cross-tree safety during parallel upass.** `upass/func_extract`
  handles imports in two phases (parallel per-LNAST, then top-down
  resolve — see `../../import.md`). Phase 1 may not read another
  tree's body or tree_ios unless the Forest marks that tree as already
  complete from a prior compilation (`cache_origin == incremental`).
  Phase 2 starts once every reachable tree reports `local_done`. The
  Forest extension that carries `local_done` / `cache_origin` /
  `inline_reason` per tree is tracked in `hhds_migration.md §8`.

## 5. Verification — LEC strategy

LEC is the cross-cutting verification gate (`simulation.md`'s "LEC as
a gate, always on"). The LGraph LEC pass is escalating, not
declarative.

### 5.1 Per-partition LEC tiers

| Boundary | Strategy | Cost |
|---|---|---|
| `comb` vs `comb` (same `interface_hash`) | Direct combinational equivalence | Fast |
| `pipe[N]` vs `pipe[N]`, same ports | Symbolic time-shift, then combinational | Fast |
| `pipe[N]` vs `pipe[M]`, N ≠ M, both in legal range | Latency-insensitive under retiming contract | Medium |
| `mod` with `ext` memories | Equivalence assuming each `ext` is functionally identical | Comb cost on the partition's logic |
| `mod`, no `ext`, large state | Partition further; else fall back to escalation (§5.2) | Slow |

### 5.2 Memory escalation

The LGraph LEC pass tries memory-as-external first: memory ports are
promoted to top-level I/O for the comparison. If equivalence holds,
the result is LEC-pass. If it fails as external, the pass escalates
to "with memory contents considered" — slower, ambiguous failure
mode. The pass reports the tier it used and whether escalation was
triggered, so the agent can interpret the result.

```jsonl
{"step":"lec","tier":"external","result":"pass","partition":"alu"}
{"step":"lec","tier":"external","result":"fail","escalated":true,
 "next_tier":"with-contents","partition":"cpu"}
```

This mechanism is symmetric: it is the same abstraction strategy
applied to imported Verilog modules (§7) and to user `ext`
declarations.

## 6. Source preservation

Source anchors are mandatory on a small, load-bearing set; everything
else uses source-map indirection.

### 6.1 Mandatory LOC

- Every `Partition::decl_loc`.
- Every `Port::decl_loc`.
- Every register declaration.
- Every memory declaration.

These survive every pass. Tests assert presence post-upass and
post-`lnast2lgraph`.

### 6.2 Source map for the rest

Internal wires, SSA temps, and synthesised cells use a source-map
indirection: one canonical map built from the original Pyrope, with
internal names pointing at one or more source locations (alias
`a = b` references both `a` and `b`'s sites). Synthesised cells with
no surface name fall back to the enclosing partition's anchor.

Open implementation details are tracked in `TODO.md` under "Source
location (LOC) propagation strategy".

### 6.3 sigref resolution

`sigref("alu.flag_z")` resolves through the partition tree:
`alu` is a partition path, `flag_z` is a port or named declaration in
that partition. Hot-probe injection at replay (`simulation.md`)
depends on partition + port naming being stable across optimisation —
which they are, by being declared at the partition boundary.

## 7. Ingress policy

| Path | Status | Role |
|---|---|---|
| `inou/prp` (Pyrope) | **Primary** | Agent-loop input. All design-centre work targets this path. |
| `inou/yosys` (Verilog → LGraph) | Testing substrate | Provides independent input to LGraph passes; not on the agent loop. |
| `inou/pyrope` (legacy PRP) | Frozen | Sunset on `inou/prp` parity. |
| `inou/slang` (SystemVerilog) | Experimental | Language-portability sanity check on LNAST. Candidate for removal. |

Verilog *egress* (`inou/cgen`) stays primary because it closes the loop
with external synth and LEC against vendor flows.

Foreign Verilog (vendor IP, hand-written modules referenced by Pyrope)
is treated as an opaque `mod` partition with declared I/O and no body —
the same handle the memory-as-external strategy uses.

## 8. Hot-reload tiers

Three tiers, one of them deliberately unsound and loudly labelled.

| Tier | Soundness | When | What survives |
|---|---|---|---|
| `hot-debug` | Sound (debug-taint static check, no state-shape change) | Observability-only edits — added `puts`, `cassert`, `cover`; toggled `debug` attr | Checkpoint preserved; sim continues without re-init |
| `hot-approx` | **Unsound by construction** — opt-in | Comb edit, state-shape unchanged. Loaded checkpoint was produced by *old* logic; new logic would not have produced it. | Checkpoint values reused; useful for "what does it look like *now*"; not for LEC |
| `cold` | Sound | Anything that changes the state shape, ports, or partition kind | Full restart; rely on upass cache for speed |

The CLI reports tier in JSONL (`future_cli.md`): `{"tier":"hot-debug"}`
or `{"tier":"hot-approx","warning":"checkpoint stale"}` so the agent
knows whether the result is verification-grade or exploration-only.

### 8.1 Debug-taint and anti-pollution

Sticky `debug` / `_debug` / `_*` propagation in `attributes_spec.md
§Phase 1` is load-bearing here, not just a UX nicety. LGraph keeps the
debug-tainted slice during sim runs (so observability code executes)
but excludes it from Verilog egress, and asserts that no
debug-tainted value flows into a non-debug node — pollution check.
This is the static guarantee that `hot-debug` is sound.

## 9. Checkpoints — unified across backends

Checkpoint format is partition-keyed via `state_shape_hash`. The map
is `(partition_path, state_decl_name) → value`, agnostic of whether
the producer was `slop` or `LGraph`-Verilog. A checkpoint written by a
fast-sim run reloads into the large-sim run and vice versa — this is
the "switch between simulation modes with shared checkpoints"
property called out in the original framing and in `simulation.md`.

Three classes of state:

- `Flop` / `Latch` slots: keyed by `(partition_path, reg_name)`.
- `Memory` contents: keyed by `(partition_path, mem_name)` with a
  `(addr, value)` map.
- `Fflop` valid + data: keyed pair.

A checkpoint is compatible with a new build iff
`state_shape_hash` matches per partition. The reload path validates
hashes and refuses incompatible state with a clear error rather than
silently mis-mapping.

## 10. Where each contract plugs in

| Contract | Owns |
|---|---|
| `lnast_spec.md` | LNAST node vocabulary, producer rules, attribute syntax, SSA / tmp naming |
| `lnast2lgraph.md` | LNAST → LGraph lowering, cell vocabulary, pin layouts, partition binding |
| `attributes_spec.md` | Attribute pass design, sticky propagation, category A/B/C/D semantics |
| `simulation.md` | Agent-native debug surface, `test::[…]`/`assert::[…]`, checkpoints, hot-probe injection |
| `hhds_migration.md` | Tree-library substrate; HHDS Forest, Phase 7 clone / replace |
| `future_cli.md` | CLI surface, JSON output, tag-scoped introspection |

Open items live in `TODO.md`.
