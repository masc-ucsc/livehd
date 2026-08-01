---
name: pyrope
description: Write and check Pyrope (LiveHD's HDL). Use when creating or editing .prp files, translating Verilog to/from Pyrope, or answering Pyrope syntax questions.
---

# Writing Pyrope

Pyrope is a hardware description language: every construct elaborates to wires,
muxes, flops, and memories. This file is the working subset needed to *generate
correct code*. The full spec lives at <https://masc-ucsc.github.io/docs/>
(Pyrope chapters 00–15, e.g. `pyrope/01b-quick_intro/` is the human-oriented
condensation, `pyrope/15-tbd/` the not-implemented list). Always verify
generated code with `lhd` (last section).

## Ground rules

* Comments are `//` only. `;` is the same as a newline. No variable shadowing,
  anywhere.
* Every declaration starts with a kind keyword — data: `const` / `mut` /
  `wire` / `reg`; lambda: `comb` / `pipe` / `mod` / `fluid`. Prefix modifiers:
  `comptime`, `pub`. Assignment prefixes: `wrap`, `sat`. `stage[N]` is a
  `mod`-only declaration modifier.
* Every data declaration needs `= value`:
    - a concrete value (`0`, `false`, `""`, `(x=1)`) — the normal case
    - `nil` — invalid / "no value yet"; *reading* it is an error; the default
      for tuples; `reg x = nil` declares a register with **no reset**;
      `wire x = nil` forward-declares an as-yet-undriven net (see Wire below)
    - `0sb?` / `0ub?` / `0ub10??01` — unknown bits (Verilog `x`); a valid
      integer value that x-propagates (`0sb? + 1 == 0sb??`, `0sb? | 1 == 1`)
    - There is **no bare `?`**, **no `_` default**, and **no `0b` prefix**
      (write `0ub`/`0sb` explicitly).
* Names are **case-sensitive**, with an enforced style: type names are
  `Capitalized` (or all-lower ending `_t`); every other identifier is
  lowercase (single letter + digit like `a1`/`D0` may be either). `_` and
  `_0`, `_1`, … are reserved, not bindable. Backtick-escape any string —
  including keywords — as an identifier: `` `while` ``.
* `comptime` must be written explicitly (`comptime const SIZE = 16`).
  Uppercase naming carries no comptime meaning.
* Integers are unlimited-precision **signed**. `u8`, `i4`, `unsigned`,
  `int(min=0, max=300)` are range constraints on that one type
  (`u<N>` max is 2^N−1). `1K == 1024`, also `M`/`G`/`T`.
* bool and int never mix: `if x != 0 {}`, casts `int(true)`, `boolean(v#[3])`.
  `and`/`or`/`implies`/`not` are boolean-only (short-circuit); `& | ^ ~ ~& ~|
  ~^` are bitwise integer ops. `%` lowers only when cheap: power-of-two
  divisor, `% 3`, or a divisor provably larger than the dividend — anything
  else is a compile error. No exponent operator.
* Precedence is shallow — parenthesize: `3 & 4*4` is a compile error; write
  `3 & (4*4)`. Comparisons chain in one direction (`a <= b < c`).

## Lambdas (the only functions)

| kind | contract |
|------|----------|
| `comb` | Pure combinational, zero cycles. No `reg` (only `::[debug]` state). `ref` args allowed (acts as an implicit output, still combinational). Inlined when not fully typed. |
| `pipe[N]` | Fixed latency `N > 0`: every output lands exactly N cycles after its inputs; **never** a comb input→output path. A feedback `reg` is state (adds no latency); an unconditionally-written feedforward `reg` is a pipeline stage counted in N. A conditional write ⇒ state register. |
| `pipe[A..=B]` / bare `pipe` | Latency range / fully flexible; the **caller** picks via `stage[N]`. `pipe` calls are only legal inside `mod`. |
| `mod` | No constraints (Mealy, Moore, orchestrator). **Every output declares its landing cycle at the interface**: `-> (x:u8@[2], y:u8@[0])`. `@[0]` = comb feedthrough (legal in `mod`, forbidden in `pipe`); `@[]` = unconstrained opt-out; omitting `@[...]` is a compile error. |
| `fluid` | Transactional valid/retry handshakes. (TBD: parses only, no lowering.) |

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
An input may declare a **default**: `comb f(in1:u4, in2:u4=3)` — a call that
omits `in2` takes the default.

**Overloading** — overload by gathering: `const add = [add1, add2]`;
a call dispatches (comptime) to the FIRST gathered lambda that can accept it
(tuple order, no ambiguity error; no-match = compile error). "Can accept" uses
the SAME argument rules as a direct call.

**Generics** — `<...>` after the lambda name; each name binds any
**compile-time entity**: a type, a comptime constant, or a lambda. Defaults
allowed (`<T, K=1>`). Bind explicitly (`f<u8>(a=1)`), by name
(`f<T=u8, K=10>(...)`), or let type-valued generics infer (unify) from the
actuals' declared types (bare literals leave T as the unbounded int kind).
Body references work: `a + K`, `T(x)` (cast), `F(v=a)` (call a lambda-valued
generic). A generic `mod`/`pipe` mints one module per binding
(`madd__u8_K_2_...`). **The old `[...]` comptime-parameter slot is gone** —
`comb g[n:int=1](x)` / `g[3](x=2)` is now a *syntax error*; write a constant
generic: `comb g<N=1>(x:u8) -> (r) { r = x + N }` called `g<N=3>(x=a)`.
Varargs `(...args)` gather leftovers (`args[i]` / `args.NAME`). There is
**no placeholder lambda sugar** — no `_`/`_0`/`_1`; pass a named comb.

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
* `(...a, b=2)` splices in place (duplicate name = error).
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
  `string(n)`. Type-shape operands write the bare type (`x does u8`).
* Enums: `enum State = (Idle, Run, Done)` — one-hot encoding by default; any
  explicit value (or an `:int` type) switches to sequential. **Always compare
  against names** (`st == State.Idle`), never raw integers. Casts:
  `string(E.a)`, `E("a")`. Hierarchical enums are documented but do NOT
  compile yet — use flat enums.
* Ranges: `0..=7`, `0..<8`, `2..+3`, optional `step 2`; ascending only. Open
  ends in selectors (`a[1..]`); negative = distance from the end
  (`b#[1..=-2]`).

## Bit selection and reduction

```pyrope
v#[3]            // bit 3            v#[1..=4]    // bit slice (zext result)
v#sext[0..=2]    // sign-extended slice
v#|[..]  v#&[..]  v#^[..]  v#+[..]   // or/and/xor-reduce, popcount (lower to int 0/1)
trans#[0] = v#[1]   // LHS bit assign; every dest bit driven exactly once
const onehot = 1 << (1, 4, 3)        // == 0ub01_1010
```

`#[]` is bits, `[]` is tuple/array elements, `@[N]` is a cycle typecheck —
never mix them. Bit concatenation = explicit per-range LHS assigns into a typed
destination (no `{a,b}` form). Runtime bit indices (`a#[i]`) work.

## Statements

* `if c { } elif { } else { }` — also an expression form. `unique if` asserts
  mutually exclusive conditions (one-hot mux; replaces tri-state).
* `match` — **parallel-unique** case (implicit `assume` of mutual exclusivity
  *and* exhaustiveness), **NOT priority**. The `else` arm is **optional**: omit
  it when the arms already cover the whole key space; an omitted `else` lowers
  to a don't-care (unreachable). Add an explicit `else` only for a real
  catch-all value or a `cassert(false)`. A bare value means `==`. Arms: `== v`,
  `in (2,3)`, `case (a=1)`, `< 5`, `else`. If two arms can match the same
  value, the lowered `__hotmux` select is non-one-hot and the output is
  **X** — for priority/overlapping conditions use `if/elif/else`. The selector
  can declare locals: `match const t = f(); t { ... }`.
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
  A block can carry synthesis attributes (`{::[abc='...', color=2] ... }`) to
  form its own ABC partition region — semantics-free, synthesis tuning only.
* `wrap` / `sat` prefix every narrowing assignment (`wrap c = a + 1`,
  `sat d += 1`); an unannotated narrowing assignment is a compile error.

## Registers and timing

```pyrope
reg counter:u8 = 0            // '= 0' is the RESET value (nil ⇒ no reset)
const q   = counter           // bare name reads current q (snapshot first if needed)
counter += 1                  // write with plain =/+=; lands at the cycle boundary
const old = past[2](counter)  // pipelined: inserts 2 flops, shifts landing cycle by 2
```

* `past[N](x)` (design body only, literal N) is a *pipelining* operator — it
  moves the expression's cycle, so `assert(past[1](x) == x)` is ill-typed. The
  verification history sample is the (TBD) positional `past(x, n)` — a
  different operator.
* No `@[-1]`/`@[1]` register indexing, no `.[defer]` — use a `wire` (below)
  for next-state reads and backward edges.
* Register attributes at declaration:
  `reg c:u8:[clock_pin=ref clk2, reset_pin=ref rst2, sync=false, posclk=false, retime] = 3`.
  `_pin` attributes connect **wires** → they need `ref` (a comptime constant
  like `reset_pin=false` doesn't). `sync` defaults true (async reset =
  `sync=false`); `retime` lets synthesis move/merge the flop.
* Multi-cycle reset code: assign a lambda **by name** (no parens):
  `reg arr:[1024]Tag = my_reset_mod`.

## Wire — single-driver combinational nets

`wire` declares ONE combinational net with **exactly one driver** (a Verilog
continuous-assign). Unlike `mut` (last write in *program order*), a `wire` may
be **read before its driver appears textually**. Use it to close interconnect
rings and feed a register's next-state to a same-cycle consumer.

```pyrope
reg counter:u32 = 0
wire nx:u32 = nil      // forward-declared, as-yet-undriven net
nx = counter + 1       // the ONE driver (may appear later in program order)
counter = nx           // registered write
const also = nx + 1    // same-cycle consumer reads the same net
```

* **Exactly one driver.** A mux (an `if`/`match` *expression*, or mutually-
  exclusive conditional writes) counts as one driver. A second unconditional
  assignment, a never-driven net, or a partially-driven net is a compile error.
* `wire` removes *textual* ordering only, **not cyclic dataflow**: a net that
  combinationally feeds itself is a real comb loop, rejected (SCC check). A
  ring is legal only when a `reg` breaks it.

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
  with `stage[N] x = v` or `past[n](v)`. An accumulator over a pipelined unit
  is just `reg total:u32 = 0; wrap total = total + tmp@[3]; out = total`
  (state regs read q at their home stage).

## Memories

```pyrope
reg mem:[256]u32 = 0       // async mem: 0-cycle read, reset to 0
reg mem2:[16]i8 = nil      // no reset
mut scratch:[] = nil       // plain array: no persistence across cycles
mut m2:[4][8]u8 = 13       // multi-dimensional, row-major
```

Read `mem[addr]`; write `if we { mem[addr] = din }`. Indices can be enum- or
range-constrained (`mut x:[X]u3`, `mut s:[-8..<7]u3`).

**Same-cycle read/write semantics** — the `ordering` attribute (replaces the
old `fwd=` attribute): `reg mem:[4]u2:[ordering="fwd"] = 0`.

* `"program"` (default): reads/writes resolve in program order, like software
  (what `mut` arrays always do).
* `"fwd"`: transparent — a read of an address written this cycle returns the
  new data regardless of textual position.
* `"none"`: cheapest, collision is **undefined** — X in formal, random in
  simulation.
* `"old"`: a read always returns the *committed* (previous-cycle) value —
  defined; what an imported Verilog reg memory means.

A synchronous SRAM is an async mem with flopped address or flopped data, or a
direct `stage[1..<inf] res = __memory(cfg)` RTL instantiation.

## Verification statements

Three statements, distinguished by **who discharges the obligation**
(all parenthesized, optional trailing `"msg"`):

* `cassert(expr)` — elaboration check: must fold to true at compile time, or
  the build fails. Never reaches formal or the netlist.
* `assert(expr)` — design obligation: `pass.formal` proves it at compile time;
  what it cannot prove stays as a runtime check. `assert_always` also checks
  during reset (that is the whole reset story; there is no `always_assert`
  family).
* `assume(expr)` — a constraint the tool may rely on, **proven first**: each
  assume is proven independently, and only a proven one becomes a hypothesis
  for the surrounding asserts. A refuted assume is a build error. Over a
  lambda's free inputs it cannot be proven: at the **top** module that fails
  the build (`assume-refuted`; downgrade with
  `--set compile.formal.on_refute=warn`); in an **instantiated** module it is
  deferred with a warning (the parent's drivers discharge it). `lhd formal
  verify` applies the same discipline: EVERY assume (formal-block or design)
  is checked as an assert before it is used, and a refuted check fails the
  run with a hint to spell `assume_nocheck`.
  `assume_nocheck(expr)` (formal blocks) is the explicit free environment
  constraint — assumed WITHOUT check, disclosed in the verdict table;
  `assume_nocheck_formal(expr)` is the fcore spelling of the same (it also
  warns per encounter); `assume_nocheck_synth(expr)` is a synthesis-only
  don't-care, invisible to verification.

**TRAP — guards are NOT inherited**: `if c { assert(x) }` lowers as an
*unconditional* `assert(x)` (known compiler bug, verified 2026-07-31). Always
write the guard into the condition: `assert(c implies x)`.

**TRAP — `lhd sim` does not execute design-body asserts**: only assertions
written inside a `test` block are checked at simulation time. Design-body
asserts are checked by `pass.formal` at compile time (and emitted into the
Verilog netlist); a violated design assert still reports sim PASS.

`requires`/`ensures` were **removed** (they parse only to warn "no obligation
generated") — write `assume` for a precondition, `assert` for a postcondition.
`cover`/`covercase` do not exist. `optimize(...)` is no longer documented —
use `assume` (a proven assume is available to the optimizer as a don't-care).

Prints: `puts("a={a} b={}", b)` (interpolation, queued to end of cycle, legal
in `comb`), `print`, `format`. `cputs("msg")` prints at elaboration — file
top-scope only for now (inside a lambda it is an undefined call).

## Tests (`lhd sim`)

A `test` is a debug-only block named by a **dotted identifier** — the string
form `test "name"` is a syntax error now. Optional runtime parameters, no
return. The DUT is declared once as an *instance* and driven through field
access; `tick N { }` is the non-unrolling cycle loop with exactly one `step`
(the clock edge) per iteration; `clock` is the 0-based cycle index:

```pyrope
mod counter(enable:bool) -> (value:u8@[0]) {
  reg count:u8 = 0
  value = count
  if enable { wrap count += 1 }
}

test counter.held_high {
  mut acc     = counter      // one persistent instance, reset on declaration
  mut v_final = nil
  tick 20 {                  // run up to 20 cycles
    acc.enable = true        // drive this cycle's input (pre-edge)
    step                     // the clock edge
    v_final = acc.value      // sample post-edge (outputs and internal regs)
  }
  assert(v_final == 20)
}

test counter.run_for(cycles:u8=5) {   // runtime param: --arg cycles=9
  mut acc = counter
  tick cycles {
    acc.enable = true
    acc.reset  = clock < 2   // reset is just an input, driven from the index
    step
  }
}
```

Assertions inside the test are checked as it runs; the test reports failure at
the end rather than stopping.

* Parameters: `p:T=v` is optional (default used), `p:T` (or `=nil`) is
  **required** — the runner must pass `--arg p=v` or the run errors (never a
  silent 0). Params are sim-only values; they never reach hardware.
* Waiting has no primitive: `step` then `if not acc.ready { continue }` inside
  `tick N` (the bound is the timeout). `waitfor`/`spawn`/`join`/`cancel` were
  **removed** — one loop, `if`-block "tasks", no coroutines. Monitors are
  inline `if`-blocks; a golden model is a `mut` updated in lockstep.
* `step [n]` advances n cycles. String-path `peek`/`poke` work; dotted field
  access is the normal form.
* Rejected inside a `test` body: `for` loops, `.[rand]`/`.[crand]`, `past[N]`.
  A *top-scope* comptime `for` wrapping many `test` blocks works (test
  fan-out; the runner disambiguates by index).

## Formal blocks (`lhd formal verify`)

A `formal name.path { }` block is a declarative overlay: every statement is a
claim that must hold at **every cycle** (nothing procedural — no `step`/
`tick`), it never lowers to hardware or simulation, and only
`lhd formal verify` consumes it. Properties live next to the design or in a
sidecar `.prp` file. Bind the design like a test does, then state properties
over dotted paths (ports, registers through instance names):

```pyrope
// cnt.verify.prp — sidecar (never becomes hardware)
const top = import("cnt.cnt")

formal cnt.bounded {
  mut acc = top
  assume(acc.enable == 0)            // input-only: environment constraint
  assert(acc.count != 5, "frozen")
}
```

* **Blocks are independent tests**: each block's assumes constrain only its
  own asserts, so two blocks may carry mutually exclusive assumes. Design-body
  assumes are the other tier — always in force for every block. A
  contradictory assume set is named and fails the run (never silently vacuous).
* An assume over **primary inputs only** is an environment constraint
  (disclosed in the verdict); one touching **registers or outputs** is
  prove-then-use — a false claim is REFUTED, never a fake proof.
* `lhd lec` takes no formal-block sidecar (it has a single obligation);
  environment constraints for lec belong in the design itself.

## Files, visibility, instantiation

* A file's top scope is setup code, run once. Only `pub` top-scope lambdas,
  types, and constants can be imported: `const lib = import("file")` /
  `import("file.pub_name")` / `import("proj/file")`. No glob patterns.
  `pub mut` and `pub reg` are compile errors; cross-hierarchy register access
  (`regref`) is TBD — verification code reaches registers through the
  ordinary instance hierarchy instead.
* Pin the generated netlist/Verilog module name with the `lg` attribute:
  `pub comb my_top::[lg="chip_top"](...)` — pub-only, comptime string; the
  `import` key stays `my_top`; the artifact becomes importable as
  `import("lg:chip_top")`. Never invent `pub("name")`.
* A fully typed `pipe`/`mod` lowers to a module; an untyped one is a per-call
  template (every actual feeding it must have a declared type). Generated
  module names are `file.entity` (e.g. `cnt.counter`).
* Do not instantiate conditionally to "save hardware": a lambda called inside
  `if`/`match` behaves as if inlined there with valid-gated inputs. Structure
  the design with unconditional calls and mux the results.

## Canonical patterns

```pyrope
// Counter — the registered-output interface form
// `mod c(...) -> (reg count:u8@[0])` does NOT lower yet; use a body register:
mod counter1(enable:bool) -> (value:u8@[0]) {
  reg count:u8 = 0
  value = count
  if enable { wrap count += 1 }
}
pipe[1] counter3(enable:bool) -> (reg count:u8) {  // pipe state output: home stage N-1
  if enable { wrap count += 1 }
}

// FSM (names are case-sensitive: `state` and type `State` coexist)
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
| `reg [7:0] x` / procedural blocking `=` | `mut x:u8 = 0` |
| `wire [7:0] x` / continuous `assign` | `wire x:u8 = ...` (single-driver net) |
| `x <= y` (non-blocking) | `x = y` on a `reg` (registered write) |
| `parameter N = 8` | `comptime const N = 8`, or a constant generic `<N=8>` |
| `always @(posedge clk)` / `@(*)` | implicit — `reg` vs `mut` |
| `case (x) ... endcase` | `match x { == v {...} else {...} }` |
| `x[6:3]` | `x#[3..=6]` |
| `{a, b}` concat | per-range LHS bit assigns into a typed dest |
| `4'b10x?` / `x` value | `0ub10??` / `0sb?` |
| one-hot mux / tri-state bus | `unique if` (lowers to `__hotmux`) |
| Verilog reg memory read semantics | `reg mem:[N]T:[ordering="old"]` |
| SVA `$rose(req) \|-> ##[1:10] $rose(ack)` | `assert(rose(req) implies rose(ack, 1..=10))` (temporal lib TBD) |
| testbench `initial` | `test name.leaf { ... tick N { ... step ... } }` |

## Gotchas — check before emitting code

1. `return X` is always wrong — assign the named output, then bare `return`.
2. Outputs must be named in `-> (...)`; no positional returns; clause is
   mandatory (except `self` methods).
3. `match` is parallel, not priority: overlapping arms ⇒ X output. `else` is
   optional when arms cover the key space; use `if/elif` for priority.
4. `when`/`unless` trailing gates and `.[defer]` no longer exist — `if` blocks
   and `wire` nets respectively.
5. `@[N]` never inserts flops (pure check); `stage[N]` inserts them
   (`mod`-only). A `mod` output without `@[N]`/`@[]` is a compile error; a
   comb path through a `pipe` is illegal.
6. No bool/int mixing: `if 5 {}` is a type error → `if 5 != 0 {}`. Reduce ops
   (`x#|[..]`) return int 0/1, not bool.
7. Narrowing assignments need `wrap`/`sat`; widths come from types, never from
   a `:[max=...]` attribute.
8. Loop bounds must be comptime (loops unroll); `tick` (test-only) is the only
   runtime-count loop. No comprehensions.
9. `0b1010` is invalid — `0ub1010`/`0sb1010`. No bare `?`/`_` initializers;
   use `nil` or `0sb?`.
10. `++` is tuple/string concat, never arithmetic. `#[]` bits vs `[]` elements
    vs `@[]` cycles.
11. Name your call arguments (`f(a=1, b=2)`); UFCS only on `self` lambdas;
    `ref` written at declaration and call.
12. The comptime `[...]` slot is a syntax error — comptime parameters are
    constant generics: `comb g<N=1>(x)`, called `g<N=3>(x=a)`.
13. `_pin` register attributes need `ref` (`clock_pin=ref clk`); reset value
    is the `= expr` initializer; `sync=false` for async reset.
14. Enum comparisons use names (`State.Idle`), never the underlying integer.
15. `if c { assert(x) }` checks `assert(x)` UNCONDITIONALLY — write
    `assert(c implies x)`.
16. Test names are dotted identifiers (`test add.basic`), never strings.
    Design-body asserts are not executed by `lhd sim` — put checked asserts in
    the `test` block.

## Not yet implemented (avoid emitting)

Valid spec Pyrope that LiveHD does not lower yet (the `pyrope/15-tbd/` doc
chapter is the authoritative list; status below re-verified against a fresh
build
2026-07-31). Do not generate these unless explicitly asked:

* `fluid` lambdas / valid-retry handshakes (parses only).
* The verification **temporal library** — `past(x, n)`, `rose(x [, w])`,
  `fell`, `stable`, `changed`, `eventually(x, w)`, `always(x, w)`. Cycle
  arguments are **positional** (there is no `f[N](x)` bracket form in this
  library) and windows are bounded ranges (`1..=10`). No
  `.[rising]`/`.[falling]`/`.[changed]` attributes. `lhd formal verify`
  rejects these with an explicit not-implemented diagnostic. (The *pipelining*
  `past[N](x)` DOES work — design body only.)
* Testbench extras: `force`/`release`, string `sigref`, `cpp("model")`
  external models, unbounded `tick`; `regref`; `assert.[failed]`.
* `cover`/`covercase`; in-language `lec()`; `.[rand]`/`.[crand]` (rejected in
  test blocks and design bodies; survive only where they constant-fold).
* `macro=` memory-compiler binding; `import("prp")` stdlib.
* **Registered-output interface form** `mod f(...) -> (reg count:u8@[0])` →
  `reg-output-cycle` error. Use a body register (`counter1` pattern above).
* **Hierarchical enums** — nested member names error; use a flat enum.

Note: the `15-tbd` chapter still lists generic constant/lambda bindings, generic
defaults, named `<T=…>` bindings, body references of a generic, and input
default values as task `3g` — all of these **already work** in the current
build (verified 2026-07-31); prefer trusting `lhd` over the TBD table there.

## Checking code with `lhd`

`lhd` is the LiveHD CLI (built in a [LiveHD](https://github.com/masc-ucsc/livehd)
checkout with `bazel build //lhd:lhd` → `bazel-bin/lhd/lhd`; prefer that
checkout binary over a copied one, which may be stale).
Stateless and deterministic; exit code 0 = pass, and the final line
is one JSON result object whose `error.class` says why it failed
(`syntax`, `equiv_fail`, `unsupported`, `internal`, …).

**Interactive-use tips** (these two save the most friction):

* Every command self-describes: `lhd help <cmd>` (== `lhd <cmd> --help`)
  prints the exact accepted args as JSON — check it before guessing flags.
  `lhd list options|steps|emit-kinds|error-classes` and `lhd describe <name>`
  enumerate the vocabulary.
* Diagnostics format: `--diag-fmt auto|jsonl|pretty` (auto = pretty on a
  terminal, JSONL when piped). **When piping through grep/head, pass
  `--diag-fmt pretty`** for clang-style text instead of JSONL; use
  `--emit diagnostics:PATH` / `--result-json PATH` when a tool consumes them.

```sh
lhd compile foo.prp                   # parse + lower + diagnostics (quick check)
lhd compile foo.prp --top NAME --emit verilog:foo.v --workdir tmp   # netlist
lhd compile foo.prp --emit-dir ln:foo_lns/     # emit IR; ln:/lg: dirs are also
lhd compile ln:foo_lns/ --emit net.v           #   valid INPUTS (compile/sim/lec)
lhd sim foo.prp                       # run every test block in the file
lhd sim foo.prp add.basic --arg n=4   # one test (dotted selector), runtime args
lhd formal verify foo.prp props.verify.prp --top foo --set formal.bound=12
lhd lec --impl foo.prp --ref gold.v --top gold --set formal.solver=cvc5
lhd pyrope fmt -i foo.prp             # formatter; `lhd pyrope lsp` = LSP server
lhd scan foo.prp                      # list the file's imports
lhd tool cat|grep|diff|tree ...       # inspect ln:/lg: artifacts
```

* **`lhd sim`** builds a C++ simulation of the `test` blocks. It needs the sim
  runtime headers — if a copied binary reports "could not locate the sim
  runtime headers (slop.hpp)", run the `bazel-bin/lhd/lhd` binary from a
  LiveHD checkout.
  Useful: `--list-tests`, `--seed N`, `--set sim.vcd=true`, `--probe SIG`,
  `--break-when 'SIG OP VALUE'`, `--vcd-on-fail`.
* **`lhd formal verify`** proves design assert/assume plus `formal` blocks by
  BMC from reset (per-obligation verdicts: PROVEN inductive vs bounded,
  REFUTED with a replay trace under `--workdir`: `formal_report.json`,
  `formalfail.prp`, VCD). Knobs: `--set formal.bound/timeout/engine/...`;
  `--formal '<glob>'` selects blocks. `formal.strict` defaults **true**: an
  inconclusive run exits nonzero (it proved nothing); a refuted obligation is
  `equiv_fail`.
* **`lhd lec`** sides are `verilog:`/`pyrope:`/`ln:`/`lg:` (bare paths: kind
  inferred). Solver: `--set formal.solver=cvc5` (default) | `bitwuzla` |
  `lgyosys`. It is sequential-aware (flop-cut induction + BMC). Vacuous
  passes are closed: an absent top or empty module is a hard `equiv_fail`.
  The backends **can disagree**: cvc5/bitwuzla reason on the LGraph, lgyosys
  on the cgen-emitted Verilog — a cgen bug or induction weakness makes one
  pass while the other fails. When a verdict surprises you, get an
  independent oracle: `--emit verilog` the netlist and simulate against the
  golden with iverilog/verilator over an exhaustive or random sweep.

Triage diagnostics by `category`: `syntax`/`name`/`type`/`bitwidth` — the
source is wrong, fix it; `unsupported` — valid Pyrope LiveHD cannot lower yet
(see the TBD list), rewrite around it, do not "fix" correct source;
`internal` — a LiveHD bug: reduce to a repro, do not change the source. The
frontend is more permissive than the spec (stale forms may still parse), so
passing `lhd compile` is necessary but not sufficient — follow this skill's
rules for style/semantics.
