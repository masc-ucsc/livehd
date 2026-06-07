# lnast2lgraph - final LNAST to LGraph generation plan

This document captures the current plan for a new final translation step from
post-upass LNAST forests to LGraph. It combines the decisions from the design
discussion with existing behavior in:

- `attribute_todo.md`
- `lgraph/cell.hpp` / `lgraph/cell.cpp`
- `inou/yosys/lgyosys_tolg.cpp`
- `inou/cgen/cgen_verilog.cpp`
- `../docs/docs/pyrope/06b-instantiation.md`
- `../docs/docs/pyrope/12-lnast.md`

The first implementation should favor clean code, small graphs, and direct use
of existing LGraph structure. Later passes can optimize the graph.

## 1. Pipeline context

`lnast2lgraph` is the **synthesis-path** lowering. See
`architecture.md` for the Y-diagram: post-upass LNAST lowers to either
this path (LGraph → synth / LEC / Verilog egress / large simulation)
or to `hlop`/`slop` (fast simulation, hot-reloadable). Both paths
share the upass trunk; do not duplicate upass work in either backend.

The compiler pipeline has three conceptual LNAST stages before LGraph
generation:

1. Frontends such as `inou.prp` or `inou.slang` produce LNAST.
2. A first local LNAST upass runs once per file, potentially in parallel.
   This pass expands tuples, expands loops, creates SSA names, and
   inlines local calls. Import handling follows the whole-file iterate
   model (`task_1m_plan.md` — the old two-phase `func_extract` /
   `Inline_reason` design is dead): an `import(...)` resolves against the
   export registry of **completed** units (this invocation's finished
   files + loaded `ln:`/`lg:` dirs); a file whose upass hits an
   unresolved *live* import defers entirely and retries after other
   files complete, so no upass ever reads a half-built tree. Resolved
   imports bind read-only values/lambda refs; calls then use the normal
   machinery (`comb` → runner inline splice, `mod`/`pipe` → Sub
   instance, `lg:` black box → Sub wired from GraphIO at `tolg`).
3. An optional global LNAST upass starts from a selected top and traverses the
   reachable forest. It performs global constant/type/attribute propagation
   across imports and may specialize imported callees per call site.

Final LGraph generation runs after the global pass. It should not discover new
specializations. At that point each reachable dependency is either:

- a final LNAST tree that must be translated to LGraph, or
- an already-existing external/direct LGraph with a compatible interface.

The final translator emits `Sub` nodes to call shared or specialized callee
graphs. It should not restart global upass work.

### 1.1 The global pass is optional

The pipeline must be usable as a 2-step flow (local upass + LGraph generation)
without the global pass. The global pass exists only as an optimization that
can produce smaller/specialized graphs through cross-module copy/constant/type
propagation and per-call-site specialization.

The 2-step flow is correct only when every imported callee LNAST has a
**fully specified input/output interface** — that is, every input and output
field of an imported module has a declared type/width: every `tree_ios`
leaf carries `bits`, `min`, `max`, and the basic type. Under that
precondition the local upass can finish each module independently, and
the final translator can emit `Sub` nodes against shared (unspecialized)
callee graphs.

The global pass is required when imported callees have generic / unconstrained
inputs or outputs. The canonical example is a Pyrope adder written without an
explicit bit-width: it cannot be translated standalone because its width is
defined by the caller. Verilog-style parametric modules fall in the same
category. In these cases the global pass propagates caller-side type/width
information into the callee and may produce per-call-site specializations.

A practical fast path for the global pass: scan imported callees and only
re-traverse / specialize those with an unknown type, incomplete
bits/min/max on a `tree_ios` leaf, or generic parameters. Fully-specified
callees can be translated once and shared.

When the global pass is disabled and an imported callee with an
incomplete interface is reached, the final translator must emit a
compile error rather than guess widths or types.

## 2. Specialization and graph names

Imports are explicit. Local calls should be handled by the first local upass;
imported/module calls remain as graph boundaries unless the global pass
specializes them.

If the global pass is disabled, no imported module is specialized.

If the global pass is enabled, a call is specialized only when the callee LNAST
changes as a result of the global pass. A reliable upass `changed` flag is
enough. A fast structural old/new tree comparison is also acceptable and can
stop at the first difference.

Naming:

- Unspecialized calls share the callee graph name, e.g. `foo`.
- Specialized calls use hierarchy/call-site names, e.g. `top.a.foo` and
  `top.b.foo`.
- If two specialized trees are exactly equal, deduplicating them to a shared
  graph is allowed. A composite/debuggable name such as `top.a|b.foo` is
  acceptable.
- LNAST tree names and generated LGraph names should match.

## 3. Final LNAST shape expected by translation

The translator should assume:

- tuples have been expanded;
- loops have been expanded;
- local calls have been inlined;
- names are SSA-unique inside each function/tree;
- only basic scalar types and persistent arrays/storage remain;
- high-level control flow may still contain `if` / `else` / `uif`;
- array reads/writes may still appear as indexed operations;
- slices, concatenation, and bit operations may still appear and are lowered
  during LGraph generation.

Tuples must not become LGraph `TupAdd` / `TupGet`. These cells are deprecated
for the new flow. A remaining tuple construction/access at final translation is
an implementation error unless it can be lowered without emitting `TupAdd` or
`TupGet`.

Flattened tuple fields should already appear as names such as `a.b`.

SSA names should be created by the first/local LNAST pass. Prefer names that
preserve file/function context and line number, with short suffixes such as
`a`, `b`, `c` for multiple generated aliases on the same source line. This
helps map LGraph back to source without storing source-location attributes.

Do not propagate LGraph `loc` / `source` metadata in the first version. The
translator may still use LNAST source metadata transiently to print compiler
warnings.

## 4. LGraph cell vocabulary

Allowed LGraph cell types come from `Ntype_op` in `lgraph/cell.hpp`:

```text
sum mult div and or xor ror not get_mask set_mask sext
lt gt eq shl sra lut mux io memory flop latch fflop sub const
tup_add tup_get attr_set attr_get compile_err
```

For this translator:

- do not generate `tup_add` or `tup_get`;
- `attr_set` and `attr_get` are valid;
- graph IO is created through `Lgraph::add_graph_input` /
  `Lgraph::add_graph_output`, not through user-created `__io`;
- `sub` is generated internally for imported/module calls, not through a
  user-facing `__sub` call;
- direct `__compile_err` is legal;
- direct `__const(a)` is legal but should behave like creating/using a
  constant value, similar to `z = a` when `a` is constant.

## 5. Method and call interfaces

Pyrope methods have explicit input/output tuples. Tuple field order defines
the LGraph interface. Use 1-based pin numbering for graph inputs and outputs
to avoid current pin-0 tuple confusion:

- input tuple field 1 maps to input pin 1;
- output tuple field 1 maps to output pin 1;
- names are useful for matching/debug, but order is the interface contract.

Multiple outputs map to multiple `Sub` driver pins in output tuple order.

### Tree-level I/O declarations

Each LNAST tree (function / module / pipe) carries its own
**`Partition` descriptor** (see `architecture.md §3`), populated by
the local upass once the body is resolved. The descriptor is the
single source of truth for I/O, latency, clock/reset domain, external
boundaries, and the two content hashes that drive incremental rebuild
and checkpoint compatibility:

- `kind` ∈ {`comb`, `pipe[N]`, `pipe[1..<N]`, `mod`} — see
  `architecture.md §3.1`. `pipe` latency is a hard contract; the
  translator must reject any body that cannot be retimed to the
  declared latency (or some value in the legal range).
- `inputs` / `outputs` are a flat, named, typed port list — never a
  tuple type. Each port has its own bits / sign / role / `decl_loc`.
- `ext` lists declared external partitions (memories, submodule
  references). Memory abstraction for LEC keys off this list.
- `interface_hash` — content hash over `(kind, latency_range, sorted
  ports, ext)`. Caller cache key.
- `state_shape_hash` — content hash over internal `reg` / `memory`
  declarations. Checkpoint compatibility key (`architecture.md §9`).

Call sites do **not** need to carry an explicit `outputs` child. The
information needed at LGraph generation lives on the callee tree, not on the
fcall. A typical fcall therefore looks like:

```lnast
fcall
  ref ___1
  ref call
  assign
    ref x
    const 1
  assign
    ref y
    ref z
```

The named `assign` children carry input mapping (this survives tuple
expansion). The result name (`___1`) is the aggregate result; later
`tuple_get` / field uses (`___1.a`, `___1[1]`, ...) are matched against the
callee's `tree_io` output list during LGraph generation.

LGraph generation contract for each fcall:

1. Resolve the callee tree by name.
2. Read the callee's `Partition` descriptor to get declared input/output
   ordering, types/widths, and `interface_hash`.
3. Validate that the fcall's named/positional inputs match the callee
   inputs (presence, count, type/width compatibility). Mismatches are
   compile errors.
4. Bind by `interface_hash`: the caller records the hash it was compiled
   against. A later edit that changes only the callee's *body* (same
   `interface_hash`) does not invalidate the caller's LGraph; an edit
   that changes the interface does. This is the incrementality contract
   from `architecture.md §4`.
5. Emit a `Sub` node and bind result fields to driver pins in callee
   output order.

If any input or output of the resolved callee has unknown type/width, the
translator cannot finish:

- if the global pass is enabled, this callee is a candidate for global
  propagation/specialization or for being inlined at the caller;
- if the global pass is disabled, this is a compile error — the callee must
  have a fully specified interface, or be inlined locally.

Calls whose result is ignored are still emitted because they may be effectful;
the unused output driver pins are simply left unread.

## 6. Direct LGraph cell calls

Every non-internal LGraph cell with a valid `cell.hpp` name should have an
explicit direct Pyrope/LNAST call spelling `__<cell_name>`, using the exact
lower-case cell name from `Ntype::get_name`.

Examples:

- `__sum`
- `__and`
- `__mux`
- `__get_mask`
- `__set_mask`
- `__sext`
- `__memory`
- `__flop`
- `__latch`
- `__attr_set`
- `__attr_get`

`__plus` is obsolete and should be renamed to `__sum`.

Reject:

- `__tup_add`
- `__tup_get`
- `__io`
- `__sub`
- invalid/internal sentinels such as `__invalid` and `__last_invalid`

Direct cell call inputs use `cell.cpp` sink pin names. Most cells have one
output named `Y`. Direct cell calls use the same fcall shape as user calls;
the cell's pin names act as the implicit `tree_io` for input matching, and
the single result pin (`Y` for most cells) is bound to the fcall's result
name. For example, before tuple expansion a variadic `or` can be:

```lnast
fcall
  ref ___1
  ref __or
  assign
    ref A
    ref some_tuple
```

After tuple expansion, tuple-valued inputs should become repeated `assign`
entries in order:

```lnast
assign
  ref A
  ref v0
assign
  ref A
  ref v1
```

Repeated input names preserve source/expanded order when connecting to LGraph
sinks.

## 7. Basic operation mapping

Most primitive operations map one-to-one to LGraph cells.

Use the pin names from `cell.cpp`:

- `Sum`, `LT`, `GT`: `A` for add/left side, `B` for subtract/right side.
- `Mult`, `And`, `Or`, `Xor`, `Ror`, `EQ`: repeated/unlimited `A`.
- `Not`: `a`.
- `Sext`, `SRA`: `a`, `b`.
- `SHL`: `a`, `B`.
- `Get_mask`: `a`, `mask`.
- `Set_mask`: `a`, `mask`, `value`.
- `Mux`: pin `0` selector, pin `1` false/default, pin `2` true for binary mux.

Both LNAST and LGraph are signed unlimited-precision by default. Width/range
is inferred by the LNAST `pass.upass bitwidth` step before LGraph creation;
LGraph nodes are created with bits/sign populated from the LNAST `max` /
`min` HHDS attrs. The legacy LGraph `pass/bitwidth` remains as a fallback
for inputs that did not come through `pass.upass bitwidth` (e.g.
Verilog-ingress paths via `inou/yosys`). Do not add nodes merely to
annotate inferred widths.

Known unsigned/range facts are represented internally as `min` / `max`;
`uN` / `ubits` lower to `min >= 0` with an associated max. The LNAST
bitwidth pass publishes `max` / `min` as HHDS attributes on LNAST
nodes/results; `bits` and `signed` are derived on demand from `max` /
`min`. `pass.lnast_to_lgraph` reads those attrs at node creation, so
bitwidth/sign do not need a separate post-lowering pass for the
LNAST-origin path.

Cutover history: prior to `pass.upass bitwidth`, the lowering deferred
to the LGraph bitwidth pass and explicit constraints (`ubits`, `sbits`,
`max`, `min`) traveled as `AttrSet` nodes. That guidance was correct
only while LNAST bitwidth did not exist; with the new pass landed,
LGraph receives bits/sign at creation and the LNAST does not need to
preserve bit attributes for downstream lowering. The LGraph bitwidth
pass still handles `max` / `min` as `AttrSet` for the Verilog-ingress
fallback path described above.

Emit an `AttrSet` after each expression assignment when the LHS variable has a
type/bit/range constraint. If the LHS has no type/bits/range constraint, do
not emit attribute nodes just for assignment bookkeeping.

Plain aliases should not emit pass-through nodes. Map the LNAST name to the
existing driver pin. A single-input `Or` is acceptable only when code structure
truly needs a materialized name, but the preferred graph is smaller and skips
extra nodes.

`const y = x` does not copy the type/constraints of `x`. A typed alias such as
`const y:x = x` explicitly gives `y` the type/constraints of `x`.

## 8. Nil and boolean representation

Every LNAST `nil` translates to LGraph constant `0sb?`. This applies to:

- explicit nil expressions;
- invalidation assignments;
- default/missing-else paths that propagate a nil value;
- reset/initial unknown values.

LGraph has no separate nil value.

Booleans are 1-bit signed integers:

- `0` is false;
- `-1` is true.

Pyrope does not allow implicit truthiness conversion. For example, `if a` is a
compile error when `a` is an integer. The source must say `if a != 0`. The
LGraph translator should not insert `Ror` or other conversions for `if`
conditions.

Boolean `and` / `or` accept only booleans, so they map directly to LGraph
`And` / `Or`.

Invalid type mixes, such as comparing integer to boolean or `bool < bool`, are
LNAST/upass errors. The translator should not repair them.

`!=`, `<=`, and `>=` may lower to existing cells:

- `a != b` -> `Not(EQ(a,b))`
- `a <= b` -> `Not(GT(a,b))` or equivalent
- `a >= b` -> `Not(LT(a,b))` or equivalent

## 9. Division, modulo, and shifts

Do not emit raw LGraph `Div` for normal Pyrope division. Hardware division is
not reasonable by default.

Division/modulo lowering:

- If `a / b` has compile-time `b` that is a power of two and `a` is known
  non-negative, lower to `SRA(a, log2(b))`.
- If `a % b` has compile-time `b` that is a power of two and `a` is known
  non-negative, lower to `Get_mask(a, b - 1)`.
- Otherwise emit a `Sub` call to an existing generic unlimited-precision
  `div` or `mod` graph and print a compiler warning:

```text
WARNING: Division generated at file <x> line <z>, this is not synthesizable
```

Use LNAST source metadata transiently for the warning; do not store this in
the generated LGraph.

The generic `div` / `mod` graphs are assumed to exist. Missing graphs are
compile errors.

Signed division/remainder semantics should stay consistent with Rust/C++ and
Verilog where they agree: signed division truncates toward zero and remainder
keeps the dividend sign. Because arithmetic shift of negative values can
round differently, the power-of-two optimization is initially restricted to
known non-negative dividends. A later implementation may add correction muxes
for negative signed values.

There is no logical right shift primitive. Logical right shift should be
expressed as `Get_mask` followed by `SRA`. Plain right shift maps to `SRA`.

## 10. Slices, concatenation, and bit operations

LNAST may still contain high-level slices/indexing/concatenation at final
translation. Lower these during LGraph generation:

- slicing: `SRA` by the offset plus `Get_mask` for unsigned slices or `Sext`
  for signed slices;
- concatenation: use shifts plus `Set_mask` / `Get_mask` / `Or`-style
  composition as needed;
- bit extraction/replacement: use `Get_mask`, `Set_mask`, and `Sext`.

This mirrors the style in `inou/yosys/lgyosys_tolg.cpp`, which lowers RTLIL
pick/concat behavior through `SRA`, `And`, `Get_mask`, and `Sext`.

## 11. Control flow lowering

The first LNAST pass does not lower `if` to muxes. The final LGraph translator
is responsible for converting `if` / `else` / `elif` / `uif` chains to muxes.
`match` should already be expanded to a sequence of unique if/else logic.

Use existing binary `Mux` chains first. `inou/cgen/cgen_verilog.cpp`
`process_mux` confirms the convention:

- pin `0`: selector
- pin `1`: false / else value
- pin `2`: true / then value

For an `if` / `elif` / `else` priority chain:

```text
if c1 { z = a }
elif c2 { z = b }
else { z = d }
```

lower as:

```text
tmp = mux(c2, d, b)
z   = mux(c1, tmp, a)
```

For a missing final `else`, the default false path is the incoming value of
the destination, equivalent to `else { z = z }`. If that incoming value is nil,
it becomes `0sb?`.

`uif` means branch conditions are mutually exclusive. A future `HotMux` cell
would represent this more directly, but do not add it in the initial
translator.

Add a TODO/FIXME in the `uif` lowering path:

```cpp
// FIXME: uif should lower to HotMux once LGraph grows that cell/semantics.
// For now emit a correct binary Mux chain.
```

The binary mux chain is valid but not always efficient.

## 12. Registers and defer

`reg` marks persistence.

- A scalar/non-indexed `reg` lowers to `Flop` by default.
- An indexed persistent `reg` array lowers to `Memory`.
- Explicit `__latch(...)` lowers to `Latch`.
- `Fflop` / fluid flop is future work, although direct `__fflop` may exist
  structurally later.

`defer` is not an LGraph node or attribute. It is a wire-level end-of-cycle
binding: `x.[defer]` reads or writes the *final* value of `x` for this cycle,
after all in-cycle assignments are resolved. It applies to plain wires as
well as regs — defer is what enables intra-cycle loops, which most often
appear around a register but can also exist on a pure wire (an intentional
ring; a combinational loop with no register in it is otherwise a bug).

For regs, the wiring rule is:

- normal reads of a scalar `reg r` use the flop output (`q`);
- the final `r.[defer]` / next-state value connects to `Flop.din`.

For plain wires, `defer` selects the cycle-final value:

```
f1 = 100
f1 = 200
f2 = f1.[defer]   // f2 reads 200 (last assignment to f1 wins)
```

```
f1 = 200
f1.[defer] = 300  // forces the cycle-final value of f1 to 300
f1 = 400          // discarded if f1 is not otherwise read; if f1 feeds a
                  // reg, the reg sees 300, not 400
```

For scalar `reg r = init`:

- create one `Flop` node;
- connect `initial` to `init` (`0sb?` for nil);
- connect normal `r` reads to the flop output;
- connect the final deferred/next-state value to `din`;
- connect `enable` to true if always written;
- connect `enable` to the write condition or OR of write conditions for
  conditional writes.

No-write hold is handled by `enable=false`; do not insert a `din=q` feedback
mux merely to preserve state.

Sequential conditional writes use last-write-wins semantics for `din`, with
`enable` indicating whether any write happened. Example:

```pyrope
if c1 { r = a }
if c2 { r = b }
```

lowers to:

```text
din    = mux(c2, a, b)
enable = c1 | c2
```

For an `if` / `elif` write chain:

```pyrope
if c1 { r = a }
elif c2 { r = b }
```

use:

```text
din    = mux(c1, b, a)
enable = c1 | c2
```

When neither condition is true, `din` is ignored.

### Flop pins

From `cell.cpp`:

| pin | name |
| --- | --- |
| 0 | `async` |
| 1 | `initial` |
| 2 | `clock_pin` |
| 3 | `din` |
| 4 | `enable` |
| 5 | `negreset` |
| 6 | `posclk` |
| 7 | `reset_pin` |

Defaults:

- `clock_pin`: if exactly one of the inputs `clk` or `clock` exists, default
  to it. If both exist or neither exists, the reg must specify `clock_pin`
  explicitly.
- `reset_pin`: if exactly one of the inputs `reset`, `rst`, `reset_n`, or
  `rst_n` exists, default to it (the `_n` variants are active-low — flip
  `negreset` accordingly). If more than one matches or none does, an
  explicit `reset_pin` is required when the reg has a non-nil initial
  value.
- `initial`: explicit init value, or `0sb?` for nil/no explicit init.
- `posclk`: true.
- `async`: false.
- `negreset`: false.

Reset rules:

- `reg a = nil`: `reset_pin=false`, `initial=0sb?`.
- `reg a = 0` with exactly one implicit reset input (`reset` / `rst` /
  `reset_n` / `rst_n`): connect that wire to `reset_pin` (set `negreset` for
  `_n` variants), `initial=0`.
- `reg a = 0` with multiple implicit reset candidates: compile error
  (require explicit `reset_pin`).
- `reg a = 0` with no implicit/explicit reset: compile error.
- `reg a:[reset_pin=false] = 0`: compile error.
- `reg a:[reset_pin=false] = nil`: valid.
- explicit `reset_pin=ref rst` with `reg a = nil`: connect reset and
  `initial=0sb?`; unknown reset value is still meaningful.
- `clock_pin=false`: compile error for storage.

### 12.1 Pipe output flops (task 1q)

**Flop depth is a cell parameter — there is no replication.** A depth-N
pipeline flop is one `Flop` cell with const-tied `pipe_min` / `pipe_max`
input pins (same style as `posclk`/`async`). Unset ⇒ depth 1, don't
check; `min==max==d` ⇒ fixed depth-d shift; `min<max` ⇒ the tool owns the
depth choice in the range (a chosen depth of 0 resolves to a wire). The
slop simulation cell carries the same parameter (efficient N-deep shift,
seed-rolled ranged depth); `inou/cgen` emits the d-stage shift register
from the single cell (range realization default at Verilog emission:
`min`); LG passes may materialize/move stages later (retiming). We
*indicate* depth; we never cut&paste flops.

Responsibility split across the flow:

- **LN pipe upass** inserts the output flop — a `stages`-annotated reg —
  when needed (skipped when the output is directly a `reg`, the counter
  idiom), with the *provisional* declared range `(min..=max)`. Insertion
  happens at LN so both consumers see it: tolg→LG→Verilog and
  `lnast_to_slop`→simulation.
- **tolg** lowers that reg to the parameterized `Flop` (this subsection's
  pin rules) like any other reg.
- **LG pass1 (check)** runs right after tolg, while name→pin maps are
  alive for diagnostics; read-only except narrowing:
    1. Tarjan SCC (flop din→q edges included): SCC with no flop ⇒
       combinational-loop error; flop whose din→q edge is in an SCC ⇒
       **state**; flop with `enable` ≠ const-true ⇒ **state**
       (conditional write = enable-encoded hold feedback, invisible to
       SCC); else **stage**.
    2. Forward σ per dpin: input 0, const ⊥ (unifies), comb cell = meet
       of operands (all non-⊥ must be EQUAL, else error at that node),
       stage flop σ(q)=σ(din)+1, state flop σ(q)=σ(din) (pinned). σ
       excludes the LN-inserted flop (identified by its pipe pins).
    3. Per output: state-reg-as-output needs home stage == min−1
       (min==max required); otherwise σ ≤ min required (declared-range
       honesty — a caller may rely on any value in `[min,max]`), and the
       inserted flop's pins are **narrowed** to `(min−σ, max−σ)` —
       `(0,0)` is realized as a wire. Body flops are never touched.
- **LG pass2 (realize)** picks the final depth within the narrowed range
  before Verilog emission (default `min`; future retiming/PPA knob).
- **`lnast_to_slop`** (future) seed-picks one valid realization (total
  `T ∈ [min,max]`, inserted depth `T−σ`) per simulation seed.

Bare `pipe` (declared `(1,0)`, max 0 = unbounded) skips the σ>min error —
the body raises the effective minimum to `max(σ,1)` for the call-site
phase.

Inserted-flop configuration: replica of an existing body flop when one
exists (copy config verbatim — async/sync reset, none, clock, posclk);
otherwise the no-reset shape (`reset_pin` unconnected, `initial=0sb?`,
`async=false` — the `reg a = nil` rules above). Clock follows the §12
defaults; if the partition declares no `clk`/`clock` input, tolg creates
an implicit `clock` graph input. Plan: `task_1q_plan.md`.

## 13. Memory lowering

Persistent indexed `reg` arrays lower to one LGraph `Memory` node per logical
storage object. Multiple reads/writes to the same storage must connect to the
same `Memory`; creating one Memory per access is incorrect because the storage
would not be shared.

Non-persistent arrays should be expanded in the first LNAST pass, like tuples.
The final translator should not normally generate clockless async Memory for
ordinary non-persistent arrays.

The translator sees array access syntax such as:

```pyrope
a[i] = x
y = a[j]
```

and must convert it to Memory ports.

Memory port order is not semantically important because `rdport` marks each
port as read or write. Choose a simple deterministic order, such as source
traversal order. Respect the fixed LGraph Memory pin stride.

### Memory pins

From `cell.cpp`, each logical memory port uses a stride of 11 raw sink pins:

| raw pin | name | meaning |
| --- | --- | --- |
| `0 + port*11` | `addr` | runtime address |
| `2 + port*11` | `clock_pin` | runtime clock |
| `3 + port*11` | `din` | write data |
| `4 + port*11` | `enable` | port enable |
| `10 + port*11` | `rdport` | `0` write, `1` read |

Shared/config pins:

| pin | name |
| --- | --- |
| 1 | `bits` |
| 5 | `fwd` |
| 6 | `posclk` |
| 7 | `type` |
| 8 | `wensize` |
| 9 | `size` |

Read data driver pins use the absolute memory port positions. If
`rdport = (0,0,1,1)`, the first two ports are writes and the last two are
reads. The read outputs are driver pins `2` and `3`.

Memory should conceptually follow the same clock/reset/initial semantics as
flops. Current LGraph Memory may not fully support reset/initial yet; implement
against the intended semantics and fix LGraph support when needed.

Multiple writes/conflicts are handled inside the Memory implementation. The
translator should pass separate write ports rather than muxing write data
outside the Memory.

## 14. Attributes in LGraph

`AttrSet` and `AttrGet` are valid final LGraph cells.

From `cell.cpp`:

| cell | pin | name |
| --- | --- | --- |
| `AttrSet` | 0 | `parent` |
| `AttrSet` | 4 | `value` |
| `AttrSet` | 5 | `field` |
| `AttrGet` | 0 | `parent` |
| `AttrGet` | 5 | `field` |

LNAST-only attributes must be consumed before or diagnosed at LGraph
generation:

- `comptime`
- `const`
- `mut`
- `private`
- `typename`
- `key`
- `loc`
- `file`
- `crand`

If these are still pending when LNAST-to-LGraph translation sees them, emit a
compile error.

Unknown/user attributes and synthesis hints may pass through as LGraph
`AttrSet` / `AttrGet`.

For type/range constraints, the path depends on whether the LNAST has
been through `pass.upass bitwidth`:

- **LNAST-origin path (post-`pass.upass bitwidth`).** `max` / `min`
  are already attached as HHDS attrs on LNAST nodes. Read those at
  node creation and populate the LGraph node's bits/sign directly;
  no `AttrSet` is needed for `ubits` / `sbits` / `max` / `min`.
- **Fallback path (Verilog ingress / inputs that did not go through
  `pass.upass bitwidth`).** Emit `AttrSet` for `ubits` / `sbits` /
  `max` / `min` and let the legacy LGraph `pass/bitwidth` consume them.

Do not emit attribute nodes for unconstrained variables on either
path.

`wrap` / `saturate` assignment policies may be lowered by upass or by LGraph
generation. If they survive to LGraph generation and widths/ranges are not
known, generate the required mask/clamp logic using existing primitives and
runtime attr/range reads as needed. They should not remain as sticky result
attributes after the narrowing operation.

## 15. Graph creation and storage

The global pass should produce the complete reachable final forest for the
selected top. LGraph generation can then translate the forest in a dependency
aware order or in parallel where dependencies are already known.

For each LNAST tree:

1. Create or replace the matching LGraph name.
2. Add graph inputs from the method input tuple, using 1-based positions.
3. Traverse statements and build a name-to-driver map.
4. Emit operations, calls, control-flow muxes, regs, memories, attributes, and
   direct cells.
5. Add graph outputs from the method output tuple, using 1-based positions.

Use direct driver aliases for simple assignments whenever possible. Do not
insert extra nodes just to preserve every SSA alias name.

Submodule calls:

- ordinary imported/module calls generate internal `Sub` nodes;
- the target graph name is the resolved shared/specialized callee name;
- input pins follow the callee input tuple order;
- output driver pins follow the callee output tuple order;
- graph names must match LNAST names for generated callees.

## 16. High-level implementation phases

This is a coarse roadmap. Each phase will get its own detailed plan before
implementation starts.

**Phase 1 — Frontend & local-upass contract.** Land the LNAST shape this
document assumes: tuple/loop expansion, local-call inlining, SSA naming, and
per-tree `tree_io` population (declared input/output names + types/widths
for every function/module/pipe). Reject deprecated direct cells
(`__tup_add`, `__tup_get`, `__io`, `__sub`, `__plus`).

**Phase 2 — Translator skeleton.** Create/replace LGraph by name, add
1-based inputs/outputs from method tuples, build the name-to-driver map,
emit simple assignments, basic arithmetic/logic ops, constants, nil →
`0sb?`, and direct alias rewiring without pass-through nodes.

**Phase 3 — Control-flow lowering.** Convert `if` / `elif` / `else` / `uif`
chains to binary `Mux` chains using the cgen pin convention. Add the
`HotMux` FIXME for `uif`.

**Phase 4 — Registers and `defer`.** Emit `Flop` / `Latch` for scalar
`reg`, apply the defer wiring rule (reads use `q`, final next-state value
drives `din`), and lower conditional writes to last-write-wins `din` with
OR-of-conditions `enable`. Implement reset/clock pin defaults and the
reset-rule error cases.

**Phase 5 — Memory lowering.** Map persistent indexed `reg` arrays to one
`Memory` per logical storage object, emit ports with the 11-pin stride,
and route read driver pins by absolute port position.

**Phase 6 — High-level op lowering.** Slices, concatenation, bit
extract/replace, logical right shift, and `!=` / `<=` / `>=` rewrites.
Division/modulo lowering with the power-of-two `SRA` / `Get_mask`
optimization and the generic `div` / `mod` `Sub` fallback plus warning.

**Phase 7 — Direct cell calls.** Support `__sum`, `__and`, `__mux`,
`__get_mask`, `__set_mask`, `__sext`, `__memory`, `__flop`, `__latch`,
`__attr_set`, `__attr_get`, etc., using `cell.cpp` sink-pin names and
repeated-input ordering for variadic pins.

**Phase 8 — Submodule calls (unspecialized).** Resolve imported callees by
tree name (`<file>.<entity>`, the func_extract convention — also the name
behind an `ln:` url; `lg:` imports resolve in the GraphLibrary instead,
see `task_1m_plan.md`), read the callee tree's flat `tree_ios` leaf list
for input/output ordering and types, validate the fcall against it, and
emit `Sub` nodes. The leaf list is the canonical call ABI — pin order
follows the dotted-name canonical order (attributes, named alphabetical,
unnamed numerical). Refuse to translate any callee with an incomplete
interface when the global pass is disabled.

**Phase 9 — Attributes.** For LNAST-origin paths after
`pass.upass bitwidth`, read `max` / `min` HHDS attrs at node creation
and populate LGraph node bits/sign directly — no `AttrSet` needed for
type/range constraints. For fallback paths (Verilog ingress), emit
`AttrSet` for `ubits` / `sbits` / `max` / `min` so the legacy LGraph
`pass/bitwidth` can consume them. In both cases, pass through
unknown/user/synthesis attributes and diagnose leftover LNAST-only
attributes (`comptime`, `const`, `mut`, `private`, `typename`, `key`,
`loc`, `file`, `crand`). Extending the legacy `pass/bitwidth` to
consume `max` / `min` remains the fallback-path requirement.

**Phase 10 — Optional global pass.** Implement specialization, hierarchical
naming (`top.a.foo`), structural-equality dedup (`top.a|b.foo`), and the
fast scan that only re-specializes callees with non-fully-typed I/O. Wire
it as an opt-in stage between the local upass and the translator.

**Phase 11 — Polish.** `wrap` / `saturate` residual lowering when widths
are still unknown at translation time, Memory reset/initial support if
LGraph needs extending, and warning/error message cleanup.

## 17. Open implementation notes

- Add per-tree `tree_io` storage on the Forest (alongside `trees`) and have
  func_extract's local-upass phase populate it once each tree's body is
  resolved. Entries are a flat list of leaves with dotted canonical names
  (`a.foo`, `a.bar`); each leaf carries basic type + `bits` + `min` +
  `max` (or "unknown" when not yet derivable). Per-tree readiness
  tracking is no longer needed for imports — resolution only consults
  completed units (`task_1m_plan.md §5`).
- LGraph generation must look up the callee's `tree_io` leaf list to
  validate fcall inputs and to bind result fields to output positions.
  Callees with an incomplete interface either trigger the global pass or
  are a compile error when it is off.
- Reject direct calls to deprecated/internal cells (`__tup_add`, `__tup_get`,
  `__io`, `__sub`).
- Add `TODO/FIXME` at `uif` lowering for future `HotMux`.
- Extend `pass/bitwidth` to consume `AttrSet max/min`.
- Decide the exact LNAST node names for high-level slice/index/concat forms
  that the translator will lower.
- Memory reset/initial support may require LGraph Memory extensions.
- Generic `div` / `mod` external graphs must exist before code using them is
  lowered.
