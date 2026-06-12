---
name: pyrope
description: Write and check Pyrope (LiveHD's HDL). Use when creating or editing .prp files, translating Verilog to/from Pyrope, or answering Pyrope syntax questions.
---

# Writing Pyrope

Pyrope is a hardware description language: every construct elaborates to wires,
muxes, flops, and memories. This file is the working subset needed to *generate
correct code*. The full spec lives in `~/projs/docs/docs/pyrope/` (chapters
00–13; `01b-quick_intro.md` is the human-oriented condensation of this
skill). Always verify generated code with `lhd` (last section).

## Ground rules

* Comments are `//` only. `;` is the same as a newline. No variable shadowing,
  anywhere. A continuation line must not start with an alphanumeric or `(`.
* Every declaration starts with a kind keyword — data: `const` / `mut` / `reg`;
  lambda: `comb` / `pipe` / `mod` / `fluid`. Prefix modifiers: `comptime`,
  `pub`. Assignment prefixes: `wrap`, `sat`. `stage[N]` is a `mod`-only
  declaration modifier.
* Every data declaration needs `= value`:
    - a concrete value (`0`, `false`, `""`, `(x=1)`) — the normal case
    - `nil` — invalid / "no value yet"; *reading* it is an error; the default
      for tuples; `reg x = nil` declares a register with **no reset**
    - `0sb?` / `0ub?` / `0ub10??01` — unknown bits (Verilog `x`); a valid
      integer value that x-propagates (`0sb? + 1 == 0sb??`, `0sb? | 1 == 1`)
    - There is **no bare `?`**, **no `_` default**, and **no `0b` prefix**
      (write `0ub`/`0sb` explicitly).
* `comptime` must be written explicitly (`comptime const SIZE = 16`).
  Uppercase naming carries no comptime meaning.
* Integers are unlimited-precision **signed**. `u8`, `i4`, `unsigned`,
  `int(min=0, max=300)` are range constraints on that one type
  (`u<N>` max is 2^N−1). `1K == 1024`, also `M`/`G`/`T`.
* bool and int never mix: `if x != 0 {}`, casts `int(true)`, `boolean(v#[3])`.
  `and`/`or`/`implies`/`not` are boolean-only (short-circuit); `& | ^ ~ ~& ~|
  ~^` are bitwise integer ops. There is no `%` (modulo) and no exponent op.
* Precedence is shallow — parenthesize: `3 & 4*4` is a compile error; write
  `3 & (4*4)`. Comparisons chain in one direction (`a <= b < c`).

## Lambdas (the only functions)

| kind | contract |
|------|----------|
| `comb` | Pure combinational, zero cycles. No `reg` (only `::[debug]` state). `ref` args allowed (acts as an implicit output, still combinational). Inlined when not fully typed. |
| `pipe[N]` | Fixed latency `N > 0`: every output lands exactly N cycles after its inputs; **never** a comb input→output path. A feedback `reg` is state (adds no latency); an unconditionally-written feedforward `reg` is a pipeline stage counted in N. A conditional write ⇒ state register. |
| `pipe[A..=B]` / bare `pipe` | Latency range / fully flexible; the **caller** picks via `stage[N]`. `pipe` calls are only legal inside `mod`. |
| `mod` | No constraints (Mealy, Moore, orchestrator). **Every output declares its landing cycle at the interface**: `-> (x:u8@[2], y:u8@[0])`. `@[0]` = comb feedthrough (legal in `mod`, forbidden in `pipe`); `@[]` = unconstrained opt-out; omitting `@[...]` is a compile error. Registered output: `-> (reg count:u8@[0])` — the q is the output. |
| `fluid` | Transactional valid/retry handshakes (`.[valid]`, `.[retry]`, `.[fire]`). Callable only from `mod`/`fluid`. (TBD: parses only, no lowering.) |

```pyrope
comb add(a:u8, b:u8) -> (r:u9) { r = a + b }
pub comb get5() -> (v) { v = 5 }       // pub = importable from other files
```

**Outputs** — always declared **by name** in `-> (...)`; the clause is
mandatory (`-> ()` for none); only `self`-methods may omit it. The body
*assigns* the outputs — a trailing bare expression does nothing. **`return` is
a terminator only**: `return X` is a syntax error. Assign first, then `return`
(for a conditional early exit, wrap it: `if cond { return }`). Callers get a named tuple
(`r.next`); a single-output tuple auto-unwraps. Destructuring binds **by
name** — `const (b, c) = f(...)` (order irrelevant); rename with
`(x=f.b) = f(...)`. Unnamed RHS tuples destructure positionally.

**Calls** — parentheses always (`noarg()`); a bare lambda name is a value
(higher-order), never a call. Name every argument: `f(a=1, b=2)`. Unnamed is OK
only when: the lambda has a single argument, the passed variable's name equals
the parameter name, or the types are unambiguous. UFCS `x.f(args)` works only
when `f` declares `self` as first parameter; `self` binds positionally, never
`f(self=...)`; `ref self` needs a `mut` receiver. `ref` must be written at the
declaration **and** the call: `comb inc(ref a) -> () { a += 1 }` … `inc(ref y)`.

**Overloading/generics** — overload by gathering: `const add = [add1, add2]`
(TBD: dispatch not lowered yet; only `init` overload sets resolve).
Generics: `comb f<T>(a:T, b:T) -> (r)` (TBD: `<T>` body specialization
pending; untyped params work). Comptime parameters live in the `[...]`
slot: `comb g[n:int=1](x) -> (r)`, called as `g[3](x=2)`. Varargs `(...args)`
gather leftovers (`args[i]` / `args.NAME`). There is **no placeholder lambda
sugar** — no `_`/`_0`/`_1` shorthands; pass a named comb (`mymap.each(inc)`).

**Constructor** — `init` is the *only* implicit hook: a `comb`, run once at
construction (`mut x:T = v` or explicit `T(v)`); overload via
`const init = [fn1, fn2]`. There are **no getter/setter hooks** — after
construction, all reads/writes are structural. Extension methods can be added
later: `Typ.double = some_comb`.

## Tuples, arrays, types

```pyrope
mut p = (mut x:u8 = 0, mut y:u8 = 0)   // named fields use a kind keyword
const iface = (
  ,mut value:u8 = 0
  ,comb read(self) -> (v:u8) { v = self.value }
  ,comb inc(ref self)        { wrap self.value += 1 }
)
mut t = (1, 2, 3)                      // positional entries are bare values
mut arr = [1, 2, 3]                    // [] = array: all entries same type
```

* Access: `p.x`, `t[0]`, `a['r1']`. Integer indices select *positional*
  entries only; named fields are name-access only.
* `a ++ b` concatenates (same-named fields merge into a subtuple);
  `(...a, b=2)` splices in place (duplicate name = error); append to a field
  *after* the literal with `y.f ++= v` (never inside it).
* A selector `[...]` takes ONE expression (int, string, range, or a
  conditional) — `a[0,1]` is not allowed.
* Mutability: outer `const` freezes every field; inner `const` pins one field
  of a `mut` tuple.
* `type Foo = (mut color:string = "", mut value:i33 = nil)` declares a type.
  Complicated lambda types must be declared ahead with `type`, never inline.
* Structural typing operators: `does` (a covers b's structure; `u32 does u16`),
  `equals`, `case` (`does` + defined-value match; `nil`/`0sb?` wildcards),
  `is` (nominal), `has` (field), `in` (membership). Negate with `not (...)` —
  there are no `!has`/`!in`/`!and` forms.
* `:Type` annotations only at declaration sites. Check elsewhere with
  `cassert(x does T)`; convert with constructor calls: `u8(x)`, `int(s)`,
  `string(n)`. There is no `_` wildcard — outputs must be named
  (`comb() -> (res:int)`), and type-shape operands write the bare type
  (`x does u8`, not `x does _:u8`).
* Enums: `enum State = (Idle, Run, Done)` — one-hot encoding by default; any
  explicit value (or an `:int` type) switches to sequential; hierarchical
  enums nest. **Always compare against names** (`state == State.Idle`), never
  raw integers. Casts: `string(E.a)`, `E("a")`.
* Ranges: `0..=7`, `0..<8`, `2..+3`, optional `step 2`; ascending only. Open
  ends in selectors (`a[1..]`); negative = distance from the end
  (`b#[1..=-2]`).

## Bit selection and reduction

```pyrope
v#[3]            // bit 3            v#[1..=4]    // bit slice (zext result)
v#sext[0..=2]    // sign-extended slice
v#|[..]  v#&[..]  v#^[..]  v#+[..]   // or/and/xor-reduce (int 0/1), popcount
trans#[0] = v#[1]   // LHS bit assign; every dest bit driven exactly once
const onehot = 1 << (1, 4, 3)        // == 0ub01_1010
```

`#[]` is bits, `[]` is tuple/array elements, `@[N]` is a cycle typecheck —
never mix them. Bit concatenation = explicit per-range LHS assigns into a typed
destination (no `{a,b}` form).

## Statements

* `if c { } elif { } else { }` — also an expression form. `unique if` asserts
  mutually exclusive conditions (one-hot mux; replaces tri-state).
* `match` — exactly-one-branch semantics, **the `else` arm is mandatory**
  (parse error without it). A bare value means `==`. Arms: `== v`, `in (2,3)`,
  `case (a=1)`, `< 5`, `else`. The selector can declare locals:
  `match const t = f(); t { ... }`.
* There are **no `when`/`unless` trailing gates** (removed from the
  language). All gating — comptime or runtime — uses an `if` block:
  `if DEBUG { assert(x) }`, `if enable { count += 1 }`. A comptime-false
  condition folds the block away entirely.
* Loops `for i in 0..<N {}`, `while`, `loop` are fully unrolled — bounds must
  be comptime. `break`/`continue` work. Iterate tuples
  (`for (i, v) in t.enumerate()`), mutate via `for x in ref t { x += 1 }`.
  Build tuples by accumulating: `mut acc:[] = nil` … `acc ++= v` (there are no
  comprehensions).
* Code blocks `{ ... }` are expressions evaluating to their last expression
  (`mut a = {mut d=3; d+1} + 100`); they may not have outer side effects.
* `wrap` / `sat` prefix every narrowing assignment (`wrap c = a + 1`,
  `sat d += 1`); an unannotated narrowing assignment is a compile error.

## Registers and timing

```pyrope
reg counter:u8 = 0            // '= 0' is the RESET value (nil ⇒ no reset)
const q   = counter           // bare name reads current q (snapshot first if needed)
counter += 1                  // write with plain =/+=; lands at the cycle boundary
const eoc = counter.[defer]   // RHS-ONLY end-of-cycle read — same-cycle wiring, no flop (TBD: not lowered yet)
const old = past[2](counter)  // value 2 cycles ago (compiler inserts 2 flops) (TBD: not lowered yet)
```

* There is **no `x.[defer] = ...` write form**, and no `@[-1]`/`@[1]` register
  indexing — those are pre-3.0 forms.
* `.[defer]` is *wiring, not time*: it lets an earlier statement use the value
  produced by a later statement in the same cycle
  (`f1 = ring(a=a, b=f4.[defer])` to close a module ring). It never crosses a
  cycle.
* Register attributes at declaration:
  `reg c:u8:[clock_pin=ref clk2, reset_pin=ref rst2, sync=false, posclk=false, retime] = 3`.
  `_pin` attributes connect **wires** → they need `ref` (a comptime constant
  like `reset_pin=false` doesn't). `sync` defaults true (async reset =
  `sync=false`); `retime` lets synthesis move/merge the flop.
* Multi-cycle reset code: assign a lambda **by name** (no parens):
  `reg arr:[1024]Tag = my_reset_mod`.

## Pipelining inside `mod`

```pyrope
pipe mul(a:u16, b:u16) -> (c:u32) { c = a * b }
pipe add(a:u32, b:u32) -> (c:u32) { wrap c = a + b }

mod mac(in1:u16, in2:u16) -> (out:u32@[4]) {
  stage[3] tmp     = mul(a=in1, b=in2)            // RHS delivered 3 cycles later
  stage[3] in1_d   = in1                          // pure 3-cycle delay
  stage[1] out@[4] = add(a=tmp@[3], b=in1_d@[3])  // all alignments typechecked
}
```

* `stage[N]` — declaration modifier, `mod`-only, `N > 0` (`stage[0]` is an
  error; plain `=` is same-cycle). Also `stage[A..=B]` and `stage[]` (tool
  picks). N is the **latency of the RHS**, relative.
* `x@[N]` — pure cycle typecheck (absolute cycle counted from the lambda
  inputs), legal on LHS and RHS, in `mod` and `pipe` bodies. It **never
  inserts flops**; a mismatch is a compile error. `@[]` opts out.
* A bare/ranged `pipe` call must be consumed by `stage[N]`. There is no
  implicit alignment: to mix values from different cycles, delay explicitly
  with `stage[N] x = v` or `past[n](v)`.
* Accumulator over a pipelined unit (state regs read q at their home stage):

```pyrope
mod accum(in1:u16, in2:u16) -> (out:u32@[3]) {
  reg total:u32 = 0
  stage[3] tmp = mul(a=in1, b=in2)
  wrap total = total + tmp@[3]
  out = total
}
```

## Memories

```pyrope
reg mem:[256]u32 = 0       // async mem: 0-cycle read, fwd by default, reset to 0
reg mem2:[16]i8 = nil      // no reset
mut scratch:[] = nil       // plain array: no persistence across cycles
mut m2:[4][8]u8 = 13       // multi-dimensional, row-major (TBD: not lowered yet)
```

Read `mem[addr]`; write `if we { mem[addr] = din }`. Indices can be enum- or
range-constrained (`mut x:[X]u3`, `mut s:[-8..<7]u3`). A synchronous SRAM is an
async mem with flopped address or flopped data, or a direct
`stage[1..<inf] res = __memory(cfg)` RTL instantiation. Physically pooled
memories are shared with `regref("mem_pool/buf0")` — behaves like a local
`reg` (reads q, sequential by construction, one functional writer globally).

## Verification

* `assert(...)`, `cassert(...)` (must hold at compile time), `optimize(...)`
  (assert + lets synthesis assume it), `requires(...)`/`ensures(...)` (lambda
  pre/post), `cover(...)`, `covercase(GRP, cond, "msg")`. Always parenthesized;
  optional trailing message. `always_assert` etc. also check during
  reset/invalid.
* `test "name {}", arg { ... }` blocks: `step [n]`,
  `waitfor(ref cond_var, timeout=N)`, `peek`/`poke("path", v)`,
  `force`/`release`, `spawn name = { ... }` with `join`/`cancel`,
  `sigref("top/a/b")` (debug-only, any signal) and `regref` (any register in
  debug contexts).
* Temporal library (debug-only except `past`): `past[n](x)`, `next[n](x)`,
  `rose[w](x)`, `fell`, `stable`, `changed`, `eventually[1..=32](x)`,
  `always[w](x)`; attribute sugar `sig.[rising]` / `.[falling]` /
  `.[changed]`. SVA `$rose(req) |-> ##[1:10] $rose(ack)` becomes
  `assert(rose(req) implies rose[1..=10](ack))`.
* Random: `x.[rand]` (simulation) / `x.[crand]` (comptime).
  `lec(gold, impl)` checks combinational equivalence.
* `puts("a={a} b={}", b)` (interpolation in double quotes, queued to end of
  cycle, allowed in `comb`), `print`, `format`, `cputs` (compile-time print).

## Files, visibility, instantiation

* A file's top scope is setup code, run once. Only `pub` top-scope lambdas,
  types, and constants can be imported: `const lib = import("file")` /
  `import("proj/file")`. `pub mut` and `pub reg` are compile errors — use
  `regref` for cross-hierarchy registers.
* Pin the generated netlist/Verilog module name with the `lg` attribute
  (TBD: not implemented yet):
  `pub comb my_top::[lg="chip_top"](...)` — pub-only, comptime string; the
  `import` key stays `my_top`. Never invent `pub("name")`.
* A fully typed `pipe`/`mod` lowers to a module; an untyped one is a per-call
  template (every actual feeding it must have a declared type).
* Do not instantiate conditionally to "save hardware": a lambda called inside
  `if`/`match` behaves as if inlined there with valid-gated inputs. Structure
  the design with unconditional calls and mux the results when that is the
  intent.

## Canonical patterns

```pyrope
// Counter — three spellings
mod counter1(enable:bool) -> (value:u8@[0]) {
  reg count:u8 = 0
  value = count
  if enable { wrap count += 1 }
}
mod counter2(enable:bool) -> (reg count:u8@[0]) {  // registered output: q is the output
  if enable { wrap count += 1 }
}
pipe[1] counter3(enable:bool) -> (reg count:u8) {  // pipe state output: home stage N-1
  if enable { wrap count += 1 }
}

// FSM — match needs else; runtime gating uses if
enum State = (Idle, Run, Done)

mod fsm(start:bool, fin:bool) -> (busy:bool@[0]) {
  reg state:State = State.Idle
  busy = state == State.Run
  match state {
    == State.Idle { if start { state = State.Run  } }
    == State.Run  { if fin   { state = State.Done } }
    else          { state = State.Idle }
  }
}

// 1-cycle dual-port RAM
pipe[1] dpram(we:bool, waddr:u8, raddr:u8, wdata:u32) -> (rdata:u32) {
  reg mem:[256]u32 = 0
  if we { mem[waddr] = wdata }
  rdata = mem[raddr]
}
```

## Verilog ↔ Pyrope quick map

| Verilog | Pyrope |
|---------|--------|
| `module m(...)` | `mod m(...) -> (out:T@[N])` (or `pipe[N]`/`comb`) |
| `input [7:0] x` / `output [7:0] y` | `x:u8` input / `y:u8@[N]` mod output |
| `reg [7:0] x` + reset | `reg x:u8 = 0` |
| `wire [7:0] x` / blocking `=` | `mut x:u8 = 0` |
| `x <= y` (non-blocking) | `x = y` on a `reg` (defers automatically) |
| `parameter`/`localparam N = 8` | `comptime const N = 8` |
| `always @(posedge clk)` / `@(*)` | implicit — `reg` vs `mut` |
| `case (x) ... endcase` | `match x { == v {...} else {...} }` |
| `x[6:3]` | `x#[3..=6]` |
| `{a, b}` concat | per-range LHS bit assigns into a typed dest |
| `4'b10x?` / `x` value | `0ub10??` / `0sb?` |
| one-hot mux / tri-state bus | `unique if` (lowers to `__hotmux`) |
| SVA assertion | `assert(... implies eventually[w](...))` |
| testbench `initial` | `test "name" { ... step ... }` |

## Gotchas — check before emitting code

1. `return X` is always wrong — assign the named output, then bare `return`.
2. Outputs must be named in `-> (...)`; no positional returns; clause is
   mandatory (except `self` methods).
3. `match` without a final `else` arm is a parse error.
4. `when`/`unless` trailing gates no longer exist — `count += 1 when enable`
   is a syntax error; write `if enable { count += 1 }`.
5. `.[defer]` is RHS-only; there is no deferred-write form.
6. `@[N]` never inserts flops (pure check); `stage[N]` inserts them
   (`mod`-only). `pipe` calls need `stage[N]` at the call site.
7. A `mod` output without `@[N]`/`@[]` at the interface is a compile error;
   a `pipe[0]` or a comb path through a `pipe` is illegal (use `comb`).
8. No bool/int mixing: `if 5 {}` is a type error → `if 5 != 0 {}`. Reduce ops
   (`x#|[..]`) return int 0/1, not bool.
9. Narrowing assignments need `wrap`/`sat`; widths come from types
   (`int(min,max)`/`uN`), never from a `:[max=...]` attribute set.
10. Loop bounds must be comptime (loops unroll). No runtime loops, no
    comprehensions.
11. `0b1010` is invalid — `0ub1010`/`0sb1010`. Bare `?` and `_` initializers
    no longer exist; use `nil` or `0sb?`.
12. `++` is tuple/string concat, never arithmetic. `#[]` bits vs `[]`
    elements vs `@[]` cycles.
13. Name your call arguments (`f(a=1, b=2)`); UFCS only on `self` lambdas;
    `ref` written at declaration and call.
14. No getter/setter hooks; only `init`. Reads never invoke code implicitly.
15. `_pin` register attributes need `ref` (`clock_pin=ref clk`); reset value
    is the `= expr` initializer; `sync=false` for async reset.
16. Enum comparisons use names (`State.Idle`), never the underlying integer.

## Not yet implemented (avoid emitting)

Valid spec Pyrope that LiveHD does not lower yet (`15-tbd.md` in the docs is
the authoritative list). Do not generate these unless explicitly asked:

* `fluid` lambdas / valid-retry handshakes (parses only)
* `.[defer]` end-of-cycle reads
* Temporal library: `past[n]`/`next[n]`/`rose`/`fell`/`stable`/`changed`/
  `eventually`/`always`, `.[rising]`/`.[falling]`
* Testbench extras: `peek`/`poke`, `waitfor`, `force`/`release`, `sigref`,
  `spawn`/`join`/`cancel` (plain `test`/`step` works)
* Overload-gathering call dispatch (`const add = [f1, f2]; add(x)`) — only
  `init` overload sets resolve; prefer distinct names
* Generic `<T>` body specialization (untyped-parameter templates DO work)
* `import("prp")` stdlib; multi-dim memories and memory `init` contents;
  `macro=`/`lg` attributes; `.[bw_max]`/`.[bw_min]` reads; `covercase`,
  in-language `lec()`

Runtime `wrap`/`sat` and enum-typed register resets ARE implemented.
Imports: `import("file")` (all pub) or `import("file.pub_name")` (one
entry); directories use slashes; no glob patterns.

## Checking code with `lhd`

`lhd` is the LiveHD CLI (on `$PATH`; in a livehd checkout build with
`bazel build //lhd:lhd` → `./bazel-bin/lhd/lhd`). It is stateless and
deterministic; the exit code is the verdict (0 = pass).

```sh
lhd elaborate foo.prp                 # parse + frontend diagnostics (fast syntax check)
lhd compile foo.prp --top NAME --emit verilog:foo.v --workdir tmp   # full lowering
lhd scan foo.prp                      # list the file's imports
lhd lsp                               # Pyrope language server over stdio (prplsp wrapper)
```

Diagnostics render clang-style on stderr; add `--emit diagnostics:PATH` for a
JSONL stream (fields: `severity`, `code`, `category`, `pass`, `message`,
`span`, `hint`). Triage by `category`:

* `syntax` / `name` / `type` / `bitwidth` — the Pyrope source is wrong: fix it.
* `unsupported` — valid Pyrope that LiveHD cannot lower *yet* (e.g.
  `.[defer]`, temporal ops like `past[n]`, `fluid` lowering — see the
  not-yet-implemented list above). Rewrite around it for synthesis; do not
  "fix" correct source. `lhd elaborate` usually still accepts it.
* `internal` — a LiveHD bug: reduce to a repro; do not change the source.

The frontend is more permissive than the spec (it may accept stale forms the
docs forbid), so passing `lhd elaborate` is necessary but not sufficient —
follow this skill's rules for style/semantics, and use `lhd` to catch
mechanical errors. Generated module names are `file.entity` (e.g.
`cnt.counter`); pin them with the `lg` attribute when a fixed name is needed.
