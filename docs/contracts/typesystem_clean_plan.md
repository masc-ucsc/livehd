# Clean type / declaration / write model for LNAST

Single source of truth for the type-system redesign (supersedes the relevant
parts of `lnast_spec.md` §3/§4/§11 and folds in task 1t). All decisions below
are LOCKED unless a line says "(open)". Reconciled against the grammar, the
`Dlop` value model, `lnast2lgraph.md`, the comptime tests, and the memory notes
(2026-05-29; via the `stress-test-typesystem-plan` and
`reconcile-typesystem-sources` workflows).

## The three conflations being removed

1. **Two write nodes for one operation.** `assign(x,v)` and
   `tuple_set(x,a,b,v)` are the same write at depth 0 vs depth N. → one `store`.
2. **A declaration is a scattered cluster.** `mut x:u8 = 255` emits
   `attr_set(x,"type","mut")` + `type_spec(x,…)` + `assign(x,255)`; per-field
   tuple types add `tuple_get` + `attr_set(__ubits,N)`. → one `declare`.
3. **Types and attributes are conflated.** `uint`/`sint` as *basic* types;
   width smuggled through `attr_set("ubits"/"sbits",N)`; `bits`/`sign`
   sometimes stored, sometimes derived. → basic types are only
   **integer/bool/string** (+complex); for an integer the only refinement is
   the value range `(max,min)`; **bits and sign are always derived**.

`Dlop`/`Const` already gets the value layer right (`Type {Integer, Boolean,
String, Nil}` — no uint/sint, no stored sign/bits), and `lnast2lgraph.md` §7-8
already specifies the backend wants exactly this: *"bits and signed are derived
on demand from max/min"* and *"booleans are 1-bit signed integers (0=false,
-1=true)"*. The LNAST layer is being brought in line.

> **Status:** the structural model (declare/store, `prim_type_int`, validators)
> and wrap/sat-as-func-call have landed (green). Remaining: `ubits`/`sbits`
> cleanup + LGraph lowering — tracked in `TODO_livehd.md` **1t**.

## Types

### Basic kinds (the only ones)
Scalar leaf types — the final `prim_type_*` set:
```
prim_type_int      // integer + (max,min) range; replaces prim_type_uint+sint
prim_type_bool     // separate kind, fixed envelope bits=1,min=-1,max=0
prim_type_string
prim_type_range    // a range value type (e.g. 1..=5) — KEEP
prim_type_none     // the single sentinel: "no/inferred type" AND "unresolved"
                   //   (unifies today's none_type + unknown_type)
```
Complex types (a tree of type nodes; leaves are the prim_types above):
```
comp_type_tuple   comp_type_array   comp_type_lambda
ref(NamedType)    // a named type is just a `ref` to its symbol-table bundle
```
- `integer` → one **`prim_type_int( [max], [min] )`**, two *optional* const children (absent ⇒ unbounded). Sign/bits derived; sign is **not** a kind.
- **Deleted:** `prim_type_type`, `prim_type_ref`, `expr_type`, `comp_type_mixin`, `unknown_type` (folded into `prim_type_none`), `prim_type_uint`/`sint` (T5).
- **`type` and `ref` are NOT types** — they move into the `mode` enum (below):
  - `type Foo = (…)` is a *declaration whose mode is `type`* (its type slot holds the structure) — replaces `prim_type_type` and `type_def`.
  - `ref` is a **function-parameter passing modifier** (pass-by-reference instead of by-value; a mutable `ref` param acts as an extra output). It is *not* mixed into the typesystem; it rides in `mode` as a placeholder, with the precise LNAST encoding deferred to a future task (likely modeled like `comptime`). Declaration and call sites must agree on `ref`.

### Integer envelope (the single source of truth)
The type's `(max,min)` is authoritative. Sizing is expressed ONLY through the
type itself — `uN`/`sN` sugar and the **type-call** `int(max=…, min=…, bits=…)`
form (parsed as `function_call_type`: callee = the type keyword, argument =
`(max=…, …)`). The **`:[max=N]`/`::[ubits=N]` attribute syntax for sizing is
dropped** — it complicated an alternative spelling with no benefit. `:[…]`
stays only for genuine (hardware/synthesis/user) attributes.

Legal (all normalize to one `prim_type_int(max,min)`; `bits=` is an input that
reconciles to the range; an explicit param refines the sugar's bounds):
| surface | max | min | note |
|---|---|---|---|
| `u8` / `s4` | 255 / 7 | 0 / -8 | width sugar |
| `uint` / `int` | ∞ / ∞ | 0 / ∞ | unbounded |
| `int(max=3)` | 3 | ∞ | only max pinned ⇒ bits nil |
| `uint(max=200)` | 200 | 0 | |
| `uint(bits=3)` | 7 | 0 | bits→(max,min) |
| `u12(min=100)` | 4095 | 100 | u12 bounds, min refined |
| `int(max=4, min=-20)` | 4 | -20 | bits derived = 6 (holds -20) |

**Producer work (T1):** today `int(max=3)` mis-lowers to a tuple literal
`(max=3)` — the producer must recognize a type-keyword callee
(`int`/`uint`/`integer`/`unsigned`/`uN`/`sN`) in a `function_call_type` and read
`max`/`min`/`bits` from the argument tuple, reconciling to `prim_type_int`.

**Derived, never stored** (computed on `.[…]` read / at lowering):
- `signed` = NOT (`min` known ∧ `min` ≥ 0)
- `bits`   = both bounds known ? (`signed` ? `1+max(clog2(max+1),clog2(-min))` : `clog2(max+1)`) : **nil**  (so `int(max=3)` ⇒ bits nil — `typesystem.prp` case 9)
- `max`/`min` reads return the stored bounds; aggregate `.[size]` = cardinality (not width)

`ubits`/`sbits`/stored-`bits` **cease to exist**.

## The four buckets (types ≠ attributes)

| bucket | what | where |
|---|---|---|
| **TYPE** | kind + integer range `(max,min)` | the `declare`/lambda-port type slot. `max`/`min` ARE the type — `:u8`, `int(max=3)`, `int(max=N,min=0)` (type-call) all feed the same two slots; never an `attr_set`/`:[…]`. |
| **MODE** | storage `mut\|const\|reg` + `comptime` | the `declare` mode slot. `comptime` is **non-sticky** — enforced at value level (a var's own declared mode), so `mut r = k` (k comptime) ⇒ r not comptime; the producer only writes `comptime` for an explicit keyword on *that* decl. |
| **OVERFLOW** | `wrap` \| `sat`, per single write | qualifier on the `store`/`declare` write. Narrowing reads the range from the type. NOT a sticky attribute. |
| **ATTRIBUTE** (`attr_set`/`attr_get`) | genuine hw/structural attrs only | `defer`, `valid`, `clock_pin`, `reset_pin`, memory `latency`/`rdport`/`wrport`; derived **reads** `.[bits]/.[sign]/.[max]/.[min]/.[typename]/.[size]` (computed, never stored); user `_debug`. |

## Attribute disposition (every key recognized today)

From `is_builtin_attr` (`upass_attributes_tuple.cpp:58`) + the `.[…]` reader
(`upass_attributes_read.cpp:412`). Categories: **DERIVED** (computed read, never
stored), **→TYPE/→MODE/→OVERFLOW** (stop being an attribute; move onto the
node), **ATTR** (genuine, stays `attr_set`/`attr_get`).

- **DERIVED reads (keep computing, no storage):** `bits`, `ubits`, `sbits`,
  `max`, `min`, `size`, `typename`, `key`, `comptime`, and **`sign`** (now
  exposed — like `bits`/`max`/`min`/`size`: a read computed from the type, not a
  stored attr). `foo.[sign]` = `min < 0` (true for signed integers; false for
  unsigned / non-negative). **An `attr_set` of any of these derived
  reads (`bits`/`ubits`/`sbits`/`max`/`min`/`size`/`sign`/`typename`) is a
  compile error** — they are read-only views of the type.
- **→TYPE (fold into the type node, delete as stored attrs):** `ubits`, `sbits`,
  `bits` (stored), `max`/`min` *when set at a declaration*, `typename`
  (becomes the named type's identity). `range` stays as the range *type*.
- **→MODE (fold into `declare` mode enum):** `type` (mut/const/reg/await),
  `mut`, `const`, `comptime`.
- **→OVERFLOW (per-write qualifier):** `wrap`, `saturate`, `sat`.
- **ATTR — stays:**
  - hardware/wiring: `clock`, `reset`, `clock_pin`, `reset_pin`, `negreset`,
    `posclk`, `async`, `initial`, `defer`, `valid`, `din`, `enable`, `stop`,
    `lat`, `num`, `addr`, `fwd`, `wensize`, `rdport`, `inputs`, `outputs`
  - synthesis hints: `critical`, `delay`, `donttouch`, `keep`, `inp_delay`,
    `out_delay`, `max_delay`, `min_delay`, `max_load`, `max_fanout`, `max_cap`,
    `left_of`, `right_of`, `top_of`, `bottom_of`, `align_with`
  - misc/meta: `loc`, `file`, `private`, `crand`, `rand`, `debug`/`_debug` +
    sticky `_*`; and Category-D user attrs (inherit aggregate→field).
- **→TYPE (memory/register):** the memory/flop config attrs
  (`rdport`/`wensize`/`lat`/`num`/`addr`/`fwd`/`clock_pin`/`reset_pin`/`negreset`/
  `posclk`/`enable`/`din`/`async`/`initial`/…) **fold into new `prim_type_memory`
  / `prim_type_register` type nodes** whose fields mirror the `graph/cell.hpp`
  `Memory`(41)/`Flop`(43)/`Latch`/`Fflop`(47) cell pins (just as `prim_type_int`
  carries `(max,min)`). The type node is the "goal end type after the
  memory/matrix is flattened"; pre-flatten some fields are defaults. (Later
  phase — not T1; pull the exact pin set from `cell.hpp` then.)

## The two nodes

### `store( var, level0 … levelN, value )` — every write/binding
Replaces `assign` (0 levels) **and** `tuple_set` (N levels). Also the value
payload of a **named/typed field** in a tuple literal is a 0-level store (a
positional field stays a bare value). No type slot — a `store` can never retype
(structural "type only at declaration"). One `process_store` branches on arity
internally (0 ⇒ scalar/wire path; ≥1 ⇒ tuple path, must flatten pre-LGraph) and
calls exactly one path.

### `declare( var, <type>, const(mode), [value] )` — every declaration
The universal "bind a name with optional type/mode/value" node — used at
statement level, as a **typed field payload inside a `tuple_add` literal**, and
as a **field entry inside `comp_type_tuple`** (a named type). Slots:
- `<type>`: a type subtree — `prim_type_int`(+range) / `prim_type_bool` /
  `prim_type_string` / `prim_type_range` / `comp_type_tuple` / `comp_type_array`
  / `comp_type_lambda` / **`ref(NamedType)`** (a named type resolves through its
  symbol-table bundle — no `expr_type`), or `prim_type_none` when inferred.
- `const(mode)`: a single const carrying a value from the **`Lnast_mode` enum**
  (one source of truth, not ad-hoc strings parsed everywhere):
  ```
  enum Lnast_mode { mut, const, reg, mut_comptime, const_comptime, type, ref }
  ```
  (open: do `reg`/`await`/`stage` get comptime variants — no, they're runtime;
  is `await`/`stage` in this enum or separate timing modifiers?) Consumers map
  the const token ↔ enum via one `Lnast_mode::from_sv`/`to_sv` (mirrors
  `Lnast_ntype`). `mode == type` makes this a type binding (replaces `type_def`);
  `mode == ref` marks a ref parameter (encoding TBD, future task).
- `value`: optional init (absent for bare `var x:u8`).
- Target is a bare `ref`; member-path declaration is a hard error. **Declare-once**: a second `declare` on a live var is a redeclaration error (validated post-constprop, accounting for dead-branch fold).

### `wrap`/`sat`
Per-write OVERFLOW qualifier on the `store`/`declare` write (option A) — never a
re-declare (which would collide with declare-once). Sticky declaration
`var:u4:[wrap]` folds the `wrap` token into the `declare` mode; per-statement
`wrap x = v` is the qualifier on that `store`. The old
`attr_set(x,"wrap")`-after-assign sticky path is deleted (it was the
`wrap_checks` fragility; the `wrap_to_signed` sext bug is already fixed).

## Worked examples (final, consistent)

`mut x:u8 = 255` →
```
declare
├── ref 'x'
├── prim_type_int
│   ├── const '255'   // max
│   └── const '0'     // min
├── const 'mut'
└── const '255'
```

`x = (a:u2=nil, b=nil)` →  (x already declared ⇒ a `store`; typed field = `declare`, untyped field = `declare` with `none`)
```
tuple_add
├── ref '___1'
├── declare
│   ├── ref 'a'
│   ├── prim_type_int   // max=3, min=0  (u2)
│   │   ├── const '3'
│   │   └── const '0'
│   ├── const 'mut'
│   └── const 'nil'
└── declare
    ├── ref 'b'
    ├── none
    ├── const 'mut'
    └── const 'nil'
store
├── ref 'x'
└── ref '___1'
```
A positional field (`(1, 2)`) stays a bare value in `tuple_add`.

Named type + use:
```
type Foo = (a:u3=1, mut b=nil)
const xx:Foo = nil
```
→ the `type` declaration binds `Foo` (mode `type`); its type slot is the
`comp_type_tuple`. The use references `Foo` by a plain `ref` in the type slot
(resolved via `Foo`'s symbol-table bundle — no `expr_type`):
```
declare                            declare
├── ref 'Foo'                      ├── ref 'xx'
├── comp_type_tuple                ├── ref 'Foo'        // named type = ref to its bundle
│   ├── declare                    ├── const 'const'    // Lnast_mode::const
│   │   ├── ref 'a'                └── const 'nil'
│   │   ├── prim_type_int (7,0)
│   │   ├── const 'const'
│   │   └── const '1'
│   └── declare
│       ├── ref 'b'
│       ├── prim_type_none
│       ├── const 'mut'
│       └── const 'nil'
└── const 'type'                   // Lnast_mode::type — Foo is a type binding
```

## Type-node simplification (`lnast_nodes.def`) — final

| node | action |
|---|---|
| `prim_type_int` | **add** (range children) — landed |
| `prim_type_uint`/`prim_type_sint` | aliases → int in T1; **delete** T5 |
| `prim_type_boolean` → `prim_type_bool` | rename (cosmetic) |
| `prim_type_string` | keep |
| `prim_type_range` | **keep** (range value type) |
| `none_type` + `unknown_type` | **unify → `prim_type_none`** (one sentinel) |
| `prim_type_type` | **delete** → `mode == type` |
| `prim_type_ref` | **delete** → `mode == ref` (ref is a param modifier, not a type) |
| `expr_type` | **delete** → named type = `ref(NamedType)` in the type slot |
| `comp_type_mixin` | **delete** (already FIXME) |
| `comp_type_tuple`/`array`/`lambda` | keep |
| `prim_type_memory` / `prim_type_register` | **add** (later phase) — fields mirror `cell.hpp` Memory/Flop pins; absorb the memory/flop config attrs |
| `type_def` | **delete → `declare` with `mode == type`** (confirmed) |
| `declare`/`store` | added (Phase A, inert) |

`mode` is the `Lnast_mode` enum (one canonical token + `from_sv`/`to_sv`), not
scattered strings.

## Migration phases (each keeps `//inou/prp:all` green — no regressions vs the current baseline; see Status)

- **T1 — `prim_type_int` + type-call sizing + single-source derivation.**
  (`prim_type_int` node + `.[sign]` read already landed — see Status.) Remaining:
  producer recognizes the type-call `int/uint/uN/sN(max=,min=,bits=)`
  (`function_call_type` callee = type keyword) and reconciles to
  `prim_type_int(max,min)`; extend `uPass_attributes::Type_info` to carry the
  `(max,min)` range (today it stores `kind`+`bits`); teach `process_type_spec` +
  `derive_bits/max/min` to consume `prim_type_int`; make `bits`/`sign` purely
  range-derived; switch `uN`/`sN` emission to `prim_type_int`.
  **Migrate the ~6 tests off the `:[…]`/`::[…]` size-attr syntax** to type-call:
  `wrap_complex` (`::[sbits=8]`→`:s8`, `::[ubits=8]`→`:u8`), `wrap_trivial`
  (`::[ubits=12]`→`:u12`), `tuple_simple_attr` (`::[bits=5]`→`:uint(bits=5)` —
  pick sign), `phase2_attr_max_min` + `phase8_size_attrs_partial`
  (`:int:[max=N,min=M]`→`:int(max=N,min=M)`). Stop the typesystem from treating
  `max/min/bits/ubits/sbits` `:[…]` attrs as type-pinning. Green.
- **T2 — `store`.** Producer: statement `assign`/`tuple_set` → `store`; runner
  `process_store` (arity branch); update the structural checks (DCE
  `dce_is_def_producing`, ssa `stmt_has_dest`, coalescer barriers,
  `lnast_manager::is_tuple_field_key`, `lnastfmt parent_writes_pos0`,
  prp_writer, `set_function_registry`). Tuple-literal field payloads → store.
  Green.
- **T3 — `declare`.** Producer: decl cluster → one `declare`; typed tuple fields
  → `declare` payloads; named types → `comp_type_tuple` of field declares.
  `process_declare` in attributes (type_info+mode+comptime), constprop (value +
  shape-merge over declare/store payloads), bitwidth (range). Stop emitting
  `attr_set(type/comptime/ubits/sbits)` + `tuple_get`-for-field-type. Update
  `is_tuple_field_key`/shape-merge to read `declare`/`store` payloads. DCE: a
  `declare` is always live. Slang (`create_declare_bits_stmts`) emits the type
  node, not `__ubits`. Green.
- **T4 — wrap/sat-as-qualifier + Cluster-F.** Narrowing reads the range at the
  write; out-of-range comptime write without wrap/sat = hard error. Targets
  `valid_unknown_bits`, `typesystem`.
- **T5 — validator + cleanup.** lnastfmt: store/declare shape, type-only-on-
  declare, declare-once. Delete `assign`/`tuple_set`/`prim_type_uint`/`sint`/
  `ubits`/`sbits`; retire the `attr_set`-as-type path; the throwaway `store`
  name can stay (it IS the write node now).
- **T6 — LGraph lowering** (deferred): derive sign/bits from the range at cell
  creation (already the `lnast2lgraph.md §7` contract); wrap→get_mask,
  sat→mux+get_mask; flatten N-level `store`.

## Reconciliation notes (other docs/code to update)
- `lnast_spec.md` §11.3 ("keep assign separate; no collapse") and §11.4 example
  lowerings are **superseded** by the `store` unification (user decision
  2026-05-29) — update when T2 lands. lnast2lgraph's "no tuple op survives"
  still holds: an N-level `store` is the thing that must flatten.
- `__ubits`/`__sbits` (slang via `create_declare_bits_stmts`) and bare
  `ubits`/`sbits` (prp) both go away in T3 (CHANGE-SOURCE). Until then the
  attributes read path keeps a legacy fallback.
- Skill guide's sticky `::[wrap]` form is per-write-qualifier under this model;
  grammar today only parses `wrap x = v` (per-statement) — `::[wrap]` on a
  declaration folds into mode (T4 / grammar follow-up).

## Settled (all confirmed 2026-05-29)
- **mode** = `Lnast_mode` enum `{mut, const, reg, mut_comptime, const_comptime,
  type, ref}` — single canonical token + `from_sv`/`to_sv`. NO `await`/`stage`.
- **`stage`/`await` are NOT typesystem** (await deprecated). `stage[1..=4]
  foo:T = fcall(…)` lowers to a NORMAL declaration (`const foo:T = …`) PLUS a
  separate timing check:
  ```
  range __r 1 4 ; get_time __res1 foo ; in __res2 __res1 __r
  cassert __res2 "foo is not within the 1..=4 range"
  ```
  (needs a `get_time` op — separate timing feature, out of scope here.)
- **type sizing** = type-call `int(max=,min=,bits=)` + `uN`/`sN` sugar; NO `:[…]`
  size attributes (tests migrated in T1).
- **`type_def` → `declare(mode==type)`**.
- **`.[sign]`** exposed as a derived read (`min < 0` ⇒ signed); `attr_set` of any
  derived read is a compile error.
- **memory/flop config → `prim_type_memory`/`prim_type_register`** (later phase),
  fields mirror `cell.hpp`.
- `tuple_simple_attr`'s `::[bits=5]` → `:uint(bits=5)` (value 2 ⇒ unsigned).
- `derive_bits` caching over wide types = perf-only, deferred.

Design is settled enough to implement T1.
