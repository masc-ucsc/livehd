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
      `const x = nil` / `wire x = nil` forward-declare an as-yet-unbound net
      (see Wire below)
    - `0sb?` / `0ub?` / `0ub10??01` — unknown bits (Verilog `x`); a valid
      integer value that x-propagates (`0sb? + 1 == 0sb??`, `0sb? | 1 == 1`)
    - There is **no bare `?`**, **no `_` default**, and **no `0b` prefix**
      (write `0ub`/`0sb` explicitly).
* Names are **case-sensitive**, with an enforced style: type names are
  `Capitalized` (or all-lower ending `_t`); every other identifier is
  lowercase (single letter + digit like `a1`/`D0` may be either). `_` and
  `_0`, `_1`, … are reserved, not bindable. Backtick-escape any string —
  including keywords — as an identifier: `` `while` ``. **Reserved-word
  matching is CASE-SENSITIVE**: only the exact spelling is reserved, so `if`
  needs backticks but `IF`/`If` are ordinary names, and `Clock`/`Reset` (type
  words) need backticks as names while `clock`/`reset` do NOT (the minted
  `clock:Clock`, a tick's `clock`, `acc.reset = clock < 2` are all bare).
* Built-in type names are **capitalized and reserved**: `U<N>`, `S<N>` (any
  digit string: `U1`, `U8`, `S4`, `U1333`), `Unsigned`, `Signed`, `Bool`,
  `String`, `Clock`, `Reset`. They cannot name a variable, port, parameter,
  lambda or field (`const U4 = 3`, `x.U33`, `f(Clock=1)` are errors); a
  backticked one is a plain name (`` `U4` ``), never the type. The OLD
  lowercase spellings — `u`/`s`/`i` + digits (`u8`, `s4`, `i32`), `bool`,
  `boolean`, `unsigned`, `signed`, `string` — are **ordinary identifiers**
  (owner ruling 2026-09-30): legal names with no backticks (`s1`, `u4`, `i0`,
  `s2`; `S2`/`U8` are the reserved ones). Only when used as a TYPE or CAST
  (`x:u8`, `u8(x)`) and not a user-declared name is it an error: "`u8` was
  renamed `U8`".
  A backticked name uses the string escapes (`` `a\\b` `` for a backslash).
* `comptime` must be written explicitly (`comptime const SIZE = 16`).
  Uppercase naming carries no comptime meaning.
* Integers are unlimited-precision **signed**. `U8`, `S4`, `Unsigned`,
  `Signed(min=0, max=300)`, `Unsigned(max=300)` are range constraints on that
  one type (`U<N>` max is 2^N−1). `1K == 1024`, also `M`/`G`/`T`. The `int`
  type, the `int(...)` cast and the `I<N>`/`iN` types were **removed**: `x:i32` is a
  compile error ("renamed `S32`"); `I8` is not a type, just an ordinary name (as a type it is an unknown type) — use `S<N>`, `Signed`/`Unsigned`, a sized
  `U<N>(x)`/`S<N>(x)` cast, or leave the declaration untyped.
* A width computed from a comptime value or a generic is spelled
  `Unsigned(bits=N)` / `Signed(bits=N)` (`U<N>` needs a LITERAL N). It is
  valid everywhere a type goes: ports, locals, tuple field types, array
  elements (`[N]Unsigned(bits=N)`), type aliases
  (`type Row = Unsigned(bits=N)`) and generic arguments. A type word can head
  a suffix chain: `U8.[max] == 255`, `U8(x)#[0]`; a type has no fields
  (`U8.x` is an error).
* Read a declaration's width/range with attributes: `x.[bits]`, `x.[max]`,
  `x.[min]` (a typed value reports its declared type; works on inputs and
  outputs: `for i in 0..<gray.[bits]`). An untyped `const` alias of a typed
  value has that value's type (`const x = a` with `a:U8`: `x.[bits] == 8`); an
  arithmetic result stays untyped. An untyped **comptime** integer
  reports the minimal width of its value (`comptime const N = 13` →
  `N.[bits] == 4`; `0` → 1; a negative value → its signed width); an untyped
  runtime value reads `nil` (and driving hardware with that nil is an error). For an index/select width use the built-in
  `std.clog2(x)` — Verilog `$clog2` (`std.clog2(1) == 0`, `std.clog2(13) == 4`,
  `std.clog2(16) == 4`, `std.clog2(17) == 5`), comptime only, compile error for
  `x <= 0` or a runtime `x`. `std` is a built-in namespace (no `import`).
* `Bool` and integers never mix — not in expressions, not at port boundaries,
  not on instance outputs: `if x != 0 {}`; Bool→bit is `U1(flag)` (true == 1;
  also `U<N>(flag)`/`Unsigned(flag)` — `Signed(true) == -1` is a
  reinterpretation, not the idiom); bit→Bool is `Bool(v#[3])` or
  `v#[3] == 1`. `Bool` is legal on **every** port, including top-level IOs (a
  1-bit port, true == 1): bind a `U1` to a `Bool` port with `Bool(x)`, use a
  `Bool` output as a number with `U1(child.flag)`. Use `U1` ports only where
  the value is arithmetic. The type name is also the cast: `x:Bool` /
  `Bool(x)`, `x:U8` / `U8(x)`, `String(n)`.
  `and`/`or`/`implies`/`not`/`!` are boolean-only (short-circuit; `!x` of an
  integer is a compile error — write `x == 0`); `& | ^ ~` are bitwise integer
  ops. There are NO binary nand/nor/xnor operators (`a ~& b` etc.) and no
  negated reductions: write `~(a & b)`, `~(a | b)`, `~(a ^ b)`, `x#&[..] == 0`.
  `~x` needs a KNOWN type: on an UNSIGNED-typed x of width N (`U<N>`,
  `Unsigned(bits=N)`, an element of a `[M]U<N>` array, a slice `x#[..]` —
  `x#[i]` is a `U1` — a `U<N>(...)` cast, an untyped `const` alias of such, `&|^~`
  of such) it flips those N bits — `(2^N-1) - x`, a `U<N>` (`~flag` of a `U1`
  toggles) — even into a wider destination; a signed-typed x or a compile-time
  integer (`~5`, `const k = 5; ~k`) is `-x - 1`; an UNTYPED runtime x
  (`~(a + 1)`, a `mut` bound without a type, `a & 3` with a literal operand,
  an `if`/`match` expression) is a compile error — type it (`const t:U9 = a + 1`) or slice it. `%` lowers only when cheap: power-of-two
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
| `mod` | No constraints (Mealy, Moore, orchestrator). **Every output declares its landing cycle at the interface**: `-> (x:U8@[2], y:U8@[0])`. `@[0]` = comb feedthrough (legal in `mod`, forbidden in `pipe`); `@[]` = unconstrained opt-out; omitting `@[...]` is a compile error. |
| `fluid` | Transactional valid/retry handshakes. (TBD: parses only, no lowering.) |

```pyrope
comb add(a:U8, b:U8) -> (r:U9) { r = a + b }
pub comb get5() -> (v) { v = 5 }       // pub = importable from other files
```

**Outputs** — always declared **by name** in `-> (...)`; the clause is
mandatory (`-> ()` for none); only `self`-methods may omit it. The body
*assigns* the outputs — a trailing bare expression does nothing. **`return` is
a terminator only**: `return X` is a syntax error. Assign first, then `return`
(for a conditional early exit, wrap it: `if cond { return }`). Callers get a named tuple
(`r.next`); a single-output tuple auto-unwraps. Destructuring binds **by
name** — `const (b, c) = f(...)` (order irrelevant); rename with
`(x=f.b) = f(...)`. Unnamed RHS tuples destructure positionally. An output
starts as `nil` (like `mut v:U8 = nil`), so bit/lane writes into it without a
prior whole assignment are legal (`out#[i] = ...`, `v[i] = ...`).

**Calls** — parentheses always (`noarg()`); a bare lambda name is a value
(higher-order), never a call. Name every argument: `f(a=1, b=2)`. Unnamed is OK
only when: the lambda has a single argument, the passed variable's name equals
the parameter name, or the types are unambiguous. UFCS `x.f(args)` works only
when `f` declares `self` as first parameter; `self` binds positionally, never
`f(self=...)`; `ref self` needs a `mut` receiver. `ref` must be written at the
declaration **and** the call: `comb inc(ref a) -> () { a += 1 }` … `inc(ref y)`.
An input may declare a **default**: `comb f(in1:U4, in2:U4=3)` — a call that
omits `in2` takes the default; a provided argument always overrides it. The
default binds like an argument: it must fit the input's type. A `comb` default
may read earlier inputs (`b:U8 = a + 1`); a `mod`/`pipe` default is what the
caller drives the omitted port with, so it must FOLD to a compile-time
constant — a literal, a generic (`c:U8 = N * 2`, per specialization), a
`comptime const`, an attribute read (`W.[bits]`), an enum entry,
`std.clog2(N)`; `b:U8 = a + 1` on a `mod` is `default-not-comptime`.

**Argument binding follows the assignment overflow rule** (`wrap`/`sat`): an
argument whose type/range may not fit the parameter's declared type — a `U16`
into `a:U4`, or into `a:Unsigned(bits=N)` with `N=4` — is a compile error for
`comb` and `mod`, generic or not. The caller slices explicitly:
`f(a=x#[0..<4])`. A wider integer into a `Bool` port is an error too. An
untyped output of a FULLY TYPED `mod`/`pipe` (`mod cnt(a:U4) -> (o@[0])`)
carries the range its body derives (`o = a + 1` is [1, 16]; the port is that
wide), so `y:U8 = c.o` is legal and `y:U4 = c.o` is may-not-fit. A value whose
range is UNKNOWN — the untyped output of a TEMPLATE `mod` (untyped input or
generics), or one its body leaves unbounded — may not fit any typed
destination: `wrap y = c.o`, or type the output. (An untyped `comb` output is
not unknown: its range is derived.)

**Nested lambdas capture only compile-time constants** (name lookup is
lexical, and the rule is the same at file scope and inside a `comb`, `mod` or
`pipe`): a lambda declared inside another — or at file scope — sees the
enclosing `comptime const`s, every plain `const` whose value FOLDS to a
compile-time constant (`const x = 2`, `const w = N * 4` with N comptime or a
generic, `mut m = 3; const s = m + 1` in a mod body, a const accumulated by a
loop over constants), the enclosing lambda's generics, a `for` index (and its
element over a comptime iterable), imports, types and lambdas, in its body
and its signature (`comb f(a:Unsigned(bits=w))`). Reading an enclosing
**runtime** value — a `const` whose value depends on an input, a `reg`, a
`wire` or a `mod` instance (`const s = a + 1`), a `mut` read directly, or the
element of `for x in (a, b)` — is a compile error; pass it as an input. A
generic argument is a compile-time value too (`g<N=a>` with `a` an input is
an error). Declaring a local, a loop index, a test parameter, or a lambda
input/output/generic with the name of a visible enclosing const, type or
lambda is shadowing (an error), even for a file-scope `const`.

**Every read needs an accessible declaration**: reading a name nothing
declares (or that is out of scope / declared later) is a compile error at the
read (`undefined-read`) in every context — bodies, signatures and port
widths, generic defaults and call-site generic args, attributes, tuple fields
(`t.zz` on a tuple without `zz` is `unknown-field`), test blocks. It never
becomes `nil` or a hidden input.

**Overloading** — overload by gathering: `const add = [add1, add2]`;
a call dispatches (comptime) to the FIRST gathered lambda that can accept it
(tuple order, no ambiguity error; no-match = compile error). "Can accept" uses
the SAME argument rules as a direct call.

**Generics** — `<...>` after the lambda name; each name binds any
**compile-time entity**: a type, a comptime constant, or a lambda. Defaults
allowed (`<T, K=1>`). Bind explicitly (`f<U8>(a=1)`), by name
(`f<T=U8, K=10>(...)`), or let type-valued generics infer (unify) from the
actuals' declared types (bare literals leave T as the unbounded integer kind).
A binding may be a postfix form unparenthesized — a dotted field or an
attribute read (`f<N=x.[bits]>(...)`, `<N=cfg.width>`); an operator
expression, a negative literal or a call must be parenthesized (`<N=(W*2)>`,
`<N=(-3)>`, `<N=(std.clog2(D))>` — a bare call reads as a type, an error).
Body references work: `a + K`, `T(x)` (cast), `F(v=a)` (call a lambda-valued
generic). A generic default or argument names a type, lambda or comptime const
DECLARED EARLIER in the file (`<T=Pixel>` before `type Pixel` is an error). A generic `mod`/`pipe` mints one module per binding
(`madd__u8_K_2_...`). **The old `[...]` comptime-parameter slot is gone** —
`comb g[n=1](x)` / `g[3](x=2)` is now a *syntax error*; write a constant
generic: `comb g<N=1>(x:U8) -> (r) { r = x + N }` called `g<N=3>(x=a)`.
Varargs `(...args)` gather leftovers (`args[i]` / `args.NAME`). There is
**no placeholder lambda sugar** — no `_`/`_0`/`_1`; pass a named comb.

**Constructor** — `init` is the *only* implicit hook: a `comb`, run once at
construction (`mut x:T = v` or explicit `T(v)`); overload via
`const init = [fn1, fn2]`. There are **no getter/setter hooks** — after
construction, all reads/writes are structural. Extension methods can be added
later: `Typ.double = some_comb`.

## Tuples, arrays, types

```pyrope
mut p = (mut x:U8 = 0, mut y:U8 = 0)   // named fields use a kind keyword
const iface = (
  ,mut value:U8 = 0
  ,comb read(self) -> (v:U8) { v = self.value }
  ,comb inc(ref self)        { wrap self.value += 1 }
)
mut t = (1, 2, 3)                      // positional entries are bare values
mut arr = [1, 2, 3]                    // [] = array: all entries same type
```

* Access: `p.x`, `t[0]`, `a['r1']`. Integer indices select *positional*
  entries only; named fields are name-access only.
* `(...a, b=2)` splices in place (duplicate name = error).
* A selector `[...]` takes ONE expression (integer, string, range, or a
  conditional) — `a[0,1]` is not allowed.
* Mutability: outer `const` freezes every field; inner `const` pins one field
  of a `mut` tuple.
* `type Foo = (mut color:String = "", mut value:S33 = nil)` declares a type.
  Complicated lambda types must be declared ahead with `type`, never inline.
* Structural typing operators: `does` (a covers b's structure; `U32 does U16`),
  `equals`, `case` (`does` + defined-value match; `nil`/`0sb?` wildcards),
  `is` (nominal), `has` (field), `in` (membership). Negate with `not (...)` —
  there are no `!has`/`!in`/`!and` forms.
* `:Type` annotations only at declaration sites. Check elsewhere with
  `cassert(x does T)`; convert with constructor calls: `U8(x)`, `Signed(s)`,
  `String(n)`. Type-shape operands write the bare type (`x does U8`). A tuple
  of types is a value like any tuple: `(a=U8, b=S20)`.
* Enums: `enum State = (Idle, Run, Done)` — one-hot encoding by default; any
  explicit value (or an integer type) switches to sequential. **Always compare
  against names** (`st == State.Idle`), never raw integers. Casts:
  `String(E.a)`, `E("a")`, `E.a#[..]` for the bits; `Signed(E.a)` and
  `Unsigned(E.a)` expose the integer encoding. A named constant or expression
  seeds the following automatic ordinals: `comptime const c = 1;
  enum E = (a=c, b)` gives `E.b == 2`.
  An enum used as a TYPE (`mod m(c:Color)`, `reg st:Color`, `mut d:Dir`) is an
  unsigned integer as wide as its widest entry value: one-hot N entries → N
  bits, explicit/sequential values → the width of the largest, hierarchical →
  one bit per node (`Animal` → 6 bits), `enum Op:U8` → 8 bits. The level type
  may be any integer type (`enum Op:Unsigned(bits=4)`, `enum Op:Nib` with
  `type Nib = U4`); it numbers the entries sequentially. There is no
  expression form: `const Color = enum(...)` is an error (write
  `enum Color = (...)`). A one-hot enum holds at most 63
  entries (use an integer level type for more), and an enum with payloads
  cannot type a port.
* Ranges: `0..=7`, `0..<8`, `2..+3`, optional `step 2`; ascending only.
  `step` applies both in loops and to range values: `(0..<30 step 10)` is
  `(0,10,20)`; a step is a positive integer. Open ends in selectors
  (`a[1..]`); negative = distance from the end (`b#[1..=-2]`).

## Bit selection and reduction

```pyrope
v#[3]            // bit 3            v#[1..=4]    // bit slice (zext result)
v#sext[0..=2]    // sign-extended slice
v#|[..]  v#&[..]  v#^[..]  v#+[..]   // or/and/xor-reduce, popcount (integer 0/1)
trans#[0] = v#[1]   // LHS bit assign; every dest bit driven exactly once
const onehot = 1 << (1, 4, 3)        // == 0ub01_1010
const z:U9 = (a, b, c)#[..]         // a:U4, b:U3, c:U2 — `a` lands in [3..0]
```

`#[]` is bits, `[]` is tuple/array elements, `@[N]` is a cycle typecheck —
never mix them. Runtime bit indices (`a#[i]`) work, also into an array element
(`m[i]#[j] = x`) and a memory entry (`mem[a]#[0..<4] = d`).

Pack an ordered tuple or array with `p#[..]`: entry 0 occupies the least
significant declared window (so Verilog `{a, b}` is `(b, a)#[..]`). `#sext`,
reductions, and subranges work over the same packed word, including a variable
holding a tuple or array. Each window comes from the lane's declared type,
never its current value (an untyped or literal lane is rejected). Multi-field
named bundles have no bit order; spell an ordered tuple of their fields
explicitly. A destination declared for a packing must match the sum of the
lane widths. There is no `concat`/`{a,b}` form.

## Statements

* `if c { } elif { } else { }` — also an expression form. `unique if` asserts
  mutually exclusive conditions (one-hot mux; replaces tri-state).
* `match` — **parallel-unique** case (implicit `assume` of mutual exclusivity
  *and* exhaustiveness), **NOT priority**. The `else` arm is **optional**: omit
  it when the arms already cover the whole key space; an omitted `else` lowers
  to a don't-care (unreachable). Add an explicit `else` only for a real
  catch-all value or a `cassert(false)`. A bare value means `==`. Arms: `== v`,
  `in (2,3)`, `case (a=1)`, `< 5`, `else`. If two arms can match the same
  value, two `__hotmux` controls are active at once, which breaks the cell's
  one-hot obligation: `pass.formal` reports it, sim/LEC resolve it to the
  FIRST active arm, and synthesis may assume exclusivity (OR the arms) — for
  priority/overlapping conditions use `if/elif/else`. The selector can declare
  locals: `match const t = f(); t { ... }`.
* There are **no `when`/`unless` trailing gates** (removed from the
  language). All gating — comptime or runtime — uses an `if` block:
  `if DEBUG { assert(x) }`, `if enable { count += 1 }`. A comptime-false
  condition folds the block away entirely.
* Loops `for i in 0..<N {}`, `while`, `loop` are fully unrolled — bounds must
  be comptime. `break`/`continue` work. Iterate tuples
  (`for (i, v) in t` — a pair binding is the enumerate; `for (i, v, key) in t`
  adds the field name; there is no `.enumerate()`), mutate via `for x in ref t { x += 1 }`.
  Build tuples by accumulating: `mut acc:[] = nil` … `acc ++= v` (there are no
  comprehensions). A loop body may READ a `wire`, but WRITING (driving) a
  `wire` inside a loop body is an error — a body write runs once per
  iteration and breaks the single-driver rule. A body may write a memory or a
  `reg` array (`for i in 0..<4 { if we { arr[i] = d } }`).
* Code blocks `{ ... }` are expressions evaluating to their last expression
  (`mut a = {mut d=3; d+1} + 100`); they may not have outer side effects.
  A block can carry synthesis attributes (`{::[abc='...', color=2] ... }`) to
  form its own ABC partition region — semantics-free, synthesis tuning only.
* `wrap` / `sat` prefix every narrowing assignment (`wrap c = a + 1`,
  `sat d += 1`); an unannotated narrowing assignment is a compile error.

## Registers and timing

```pyrope
reg counter:U8 = 0            // '= 0' is the RESET value (nil ⇒ no reset)
const q   = counter           // bare name reads current q (snapshot first if needed)
wrap counter += 1             // write with =/+= (wrap: U8 narrows); lands at the cycle boundary
const old = past[2](counter)  // pipelined: inserts 2 flops, shifts landing cycle by 2
```

* `past[N](x)` (design body only, literal N) is a *pipelining* operator — it
  moves the expression's cycle, so `assert(past[1](x) == x)` is ill-typed. The
  verification history sample is the (TBD) positional `past(x, n)` — a
  different operator.
* No `@[-1]`/`@[1]` register indexing, no `.[defer]` — use a `wire` (below)
  for next-state reads and backward edges.
* Register attributes at declaration:
  `reg c:U8:[clock_pin=clk2, reset_pin=rst2, async=true, posclk=false] = 3`.
  A `*_pin` attribute (`clock_pin`, `reset_pin`, …) is written WITHOUT `ref`
  (`reset_pin=my_wire` is a connection, not a read of the value;
  `clock_pin=ref clk` is a compile error); `reset_pin=false` means no reset.
  `clock_pin` names a `Clock`: a `Clock` input, a gated clock
  `Clock(clock_pin=clk, enable=en)` or a child's `Clock` output, never a
  constant, a `Reset` or `Bool`/`U1` data (`clock-bind-not-clock`);
  `reset_pin` takes a `Reset` or any `Bool` expression (`reset_pin=rst or soft`).
  `async` defaults false (`async=true` = asynchronous reset; the importer's
  `sync=` is accepted with a deprecation warning — never write it). A reset is
  active-high unless the register says `negreset=true`: the reset's NAME
  carries no polarity (`rst_n` is active-high without it). The implicit-reset
  style is the `compile.upass.reset_style` flag (`sync` default | `async`); a
  per-register `async` overrides it. The reset value must be
  comptime (a `comptime const` computed by a `comb` call works; a runtime
  `mut` loop does not).
* `reg x:T:[latch=true]` declares a level-sensitive latch — the grammar has no
  `latch` keyword, and the marker is consumed at declaration (not readable back
  as `.[latch]`). `enable_high` is an alias of `posclk` accepted on ANY
  register: on a flop it is the clock edge, on a latch it is the ENABLE
  polarity. An active-low latch enable (`enable_high=false`/`posclk=false`) is
  REFUSED — write the inverted condition instead (`if !g { ... }`).
* **Clocks and resets bind by TYPE, not by name.** A `mod`/`pipe` input typed
  `Clock` is a clock and one typed `Reset` is a reset, whatever the name
  (`core_clk:Clock`, `rst_n:Reset`); an input named `clk`/`clock`/`rst`/`reset`
  typed `Bool`/`U1` is plain DATA. Implicit clock = the module's single
  `Clock` input; implicit reset = its single `Reset` input. Two or more
  `Clock` (`Reset`) inputs leave no implicit one: every register, memory and
  stage names it (`clock_pin=clk_a`, `reset_pin=r2`), and relying on it is a
  compile error (`clock-ambiguous`). With none, a `clock:Clock` /
  `reset:Reset` input is minted (a non-`Clock` input already named `clock`, or
  non-`Reset` named `reset`, is then a compile error) and every caller wires
  its own implicit clock/reset to it.
* A `Clock` is not data: no arithmetic, no `if clk`, no `U1(clk)`/`Bool(clk)`
  (`clock-as-data`); it only drives clock pins or another `Clock` port. Its
  numeric view (a cycle counter) is readable only in debug contexts (`test`
  blocks, `puts`, `assert(clk < 1000)` in a body). The ONE derived clock is a
  gated one, the ICG `const gclk = Clock(clock_pin=clk, enable=en)` (named
  arguments; lowers to a `Clock_cell`, `en` latched while `clk` is low), used as
  `reg r::[clock_pin=gclk] = 0` or bound to a child's `Clock` input; it chains
  (`Clock(clock_pin=gclk, enable=e2)`). `clk and en`, `not clk`, a clock mux,
  a register used as a clock, and a latch enable read from the clock
  (`if !clk { l = d }`) are not clocks (`Clock(x)` is not a cast). A `Reset` is Bool-like: `if rst`,
  `rst or soft`, `f = rst` work, and a `Bool` expression binds to it without a
  cast; `rst != 0` or `rst | x` do not (`U1(rst)`). `Clock` and `Reset` are
  distinct types (`Clock does Bool` is false): a Clock binds only to a `Clock`
  input (never to a `Bool` input the child reads, never to a `Reset`), a
  child's `Clock` input takes only a Clock (no derived clocks: `c=gclk_and_en`
  is `clock-bind-not-clock`), `clock_pin` never names a `Reset`, and a `Clock`
  output never takes a constant or data.
* At a call, OMIT a `mod`/`pipe` child's `Clock`/`Reset` inputs (declared or
  minted) — they auto-wire to the caller's single `Clock`/`Reset` (minted in
  the caller when it has none). The reset crosses as the RAW wire, exactly
  like an explicit binding: polarity belongs to each register (`negreset=`),
  so crossing polarities needs an explicit computed binding
  (`cnt8(rst_n=!rst)`). A caller with two or more `Clock` (`Reset`) inputs
  must bind every child `Clock` (`Reset`) input explicitly — a child's MINTED
  one by its name (`child(clock=c1, ...)`). A `comb`
  holds no state and can NOT declare a `Clock`/`Reset` input
  (`comb-clock-reset-input`): its `clk`/`rst` are plain `Bool`/`U1` DATA,
  never auto-wired. Any `comb` input its body never reads may be omitted
  (nothing is wired); omitting one it reads is `missing required argument` —
  pass it (`clr(rst=not rst_n, a=a)`).
* Binding a constant to a `Clock` is always a compile error
  (`clock_pin=k` with a constant `k`, `dut(clk=true, ...)` into a `Clock`
  input: `clock-const-bind`), whether or not a register reads it. A `Bool`
  input is data whatever its name, so `f(clk=false, ...)` into a `clk:Bool`
  is fine.
* `reg` in a `mod` OUTPUT list declares a persistent register whose q IS the
  output, `= v` after the cycle is its reset value:
  `-> (reg count:U8@[0] = 0)`. A conditional or feedback write makes it state
  (lands at `@[0]`); an unconditional write of a fresh value makes it a stage
  (lands at `@[1]`). `reg` on an INPUT is an error (inputs are driven at each
  call). A caller sees each child output at that output's own declared landing
  cycle; `@[]` opts out.
* A **body** `reg` (declared in the body, not the output list) written every
  cycle from its inputs and read by a `mod` output directly or through a bit
  slice (`q = r`, `q = r#[0..<4]`) lands one cycle after its inputs
  (`r = d; q = r` needs `@[1]`, ruling 83), with the implicit clock or an
  explicit `clock_pin`/`reset_pin`, written whole or through bit slices
  (`r#[0..<4] = d`). NOT settled (today's behavior): any other read of it
  (`q = ~r`, `q = r ^ 1`, `q = w` via a wire) is cycle-0 state (`@[0]`); a
  conditionally written or feedback body reg is state at its home stage
  (`if en { r = d }; q = r` needs `@[0]`; a feedback reg that adds a
  stage-3 value, `wrap total = total + tmp@[3]`, homes and lands at `@[3]`),
  while the same reg on a gated clock (`Clock(clock_pin=clk, enable=en)`)
  needs `@[1]`.
  When the cycle matters, declare the register in the output list or use
  `stage[N]`.
* `::[timecheck=false]` (old spelling `hdl`) on a lambda is an escape hatch
  that turns off ALL timing checks inside it (landing-cycle checks, cycle-mix
  checks incl. through children's declared latencies, the same-cycle
  wire-ring/comb-loop check); plain regs become cycle-0 state and an undriven
  `wire` reads `x`. The Verilog importer and the Pyrope writer set it;
  hand-written code uses it only as an escape hatch.
* Multi-cycle reset code: assign a lambda **by name** (no parens):
  `reg arr:[1024]Tag = my_reset_mod`.

## Wire — single-driver combinational nets

`wire` declares ONE combinational net with **exactly one driver** (a Verilog
continuous-assign). Unlike `mut` (last write in *program order*), a `wire` may
be **read before its driver appears textually**. Use it to close interconnect
rings and feed a register's next-state to a same-cycle consumer.

```pyrope
reg counter:U32 = 0
wire nx:U32 = nil      // forward-declared, as-yet-undriven net
nx = counter + 1       // the ONE driver (may appear later in program order)
counter = nx           // registered write
const also = nx + 1    // same-cycle consumer reads the same net
```

* **Exactly one driver.** A mux (an `if`/`match` *expression*, or mutually-
  exclusive conditional writes) counts as one driver. A second unconditional
  assignment or a never-driven net is a compile error. The driver may be
  CONDITIONAL and need not cover every path: a `wire` is defined by its one
  assignment, so writing it inside an `if` with no `else` means the same as
  writing it unconditionally (an un-taken path is a don't-care, not an `x`).
* A `for`/`while` body may read a `wire` but never write one (see Loops).
* `wire` removes *textual* ordering only, **not cyclic dataflow**: a net that
  combinationally feeds itself is a real comb loop, rejected (SCC check). A
  ring is legal only when a `reg` breaks it.
* `const x [:T] = nil` is the same forward declaration WITHOUT the ordering
  exemption: bound exactly once (a rebind is an error), that bind defines the
  value on every path exactly as a `wire`'s does, but it must precede every
  read. Prefer it when the write already comes first — `wire` is for the case a
  read genuinely comes before its driver.

## Pipelining inside `mod`

```pyrope
pipe mul(a:U16, b:U16) -> (c:U32) { c = a * b }
pipe add(a:U32, b:U32) -> (c:U32) { wrap c = a + b }

mod mac(in1:U16, in2:U16) -> (out:U32@[4]) {
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
  is just `reg total:U32 = 0; wrap total = total + tmp@[3]; out = total`
  (state regs read q at their home stage).

## Memories

```pyrope
reg mem:[256]U32 = 0       // async mem: 0-cycle read, reset to 0
reg mem2:[16]S8 = nil      // no reset
mut scratch:[] = nil       // plain array: no persistence across cycles
mut m2:[4][8]U8 = 13       // multi-dimensional, row-major
```

Read `mem[addr]`; write `if we { mem[addr] = din }`. Indices can be enum- or
range-constrained (`mut x:[X]U3`, `mut s:[-8..<7]U3`).

**Same-cycle read/write semantics** — the `ordering` attribute (replaces the
old `fwd=` attribute): `reg mem:[4]U2:[ordering="fwd"] = 0`.

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
  `assume_nocheck(expr)` is the explicit free environment constraint —
  assumed WITHOUT check, disclosed as UNCHECKED in the verdict;
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
in `comb`), `print`. `format(...)` was removed. `cputs("msg")` prints at
elaboration — file top-scope only for now (inside a lambda it is an undefined
call).

## Tests (`lhd sim`)

A `test` is a debug-only block named by a **dotted identifier** — the string
form `test "name"` is a syntax error now. Optional runtime parameters, no
return. The DUT is declared once as an *instance* and driven through field
access; `tick N { }` is the non-unrolling cycle loop with exactly one `step`
(the clock edge) per iteration. Each `tick` has a minted `clock:Clock` that
counts its cycles from 0 (a debug context, so `clock < 2` is legal there) and
auto-wires into the DUT's unbound `Clock` input:

```pyrope
mod counter(enable:Bool) -> (value:U8@[0]) {
  reg count:U8 = 0
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

test counter.run_for(cycles:U8=5) {   // runtime param: --arg cycles=9
  mut acc = counter
  tick cycles {
    acc.enable = true
    acc.reset  = clock < 2   // the minted Reset takes a Bool, driven from the cycle
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
* `step [n]` advances n cycles. Read any cell with a bare dotted path at any
  depth (`acc.core0.count`); drive a register through a `regref` handle bound
  once (`mut h = regref(acc.child.count)` … `h = 7`; the string form
  `regref("acc/child.count")` also works). `peek`/`poke`/`sigref` were
  removed.
* An instance handle (`mut d = dut`) is not a tuple: `d.x = v` drives input
  `x`, `d.o` reads the design, anywhere in the test (after a `tick`, after a
  bare `step`). Strict Bool holds in tests too: drive a `Bool` input with
  `true`/`false`/`Bool(x)` (`d.en = 1` is an error) and use a `Bool` output
  as an integer only through `U1(d.f)`. A poke follows the argument rules
  (`d.a = 300` into a `U8` is an error). Never poke a constant into a
  `Clock` input; a `Reset` input takes a `Bool` (`d.rst = clock < 2`). File-scope `const`s are visible (a
  test local or test parameter named like one is shadowing, an error); `lhd sim` evaluates the
  plain integer and tuple-field ones, not yet one built from `std.clog2`, an
  enum entry or a nested tuple field ("unsupported dot expression").
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
  assume_nocheck(acc.enable == false)  // free environment constraint (unchecked)
  assert(acc.count != 5, "frozen")
}
```

* **Blocks are independent tests**: each block's assumes constrain only its
  own asserts, so two blocks may carry mutually exclusive assumes. Design-body
  assumes are the other tier — always in force for every block. A
  contradictory assume set is named and fails the run (never silently vacuous).
* A plain `assume` is CHECKED as an assert before it is used (prove-then-use):
  one over free primary inputs is REFUTED (nothing forces it), one over
  registers/outputs is used only once proven. Spell an intended environment
  constraint `assume_nocheck(...)`.
* `lhd lec` takes no formal-block sidecar (it has a single obligation);
  environment constraints for lec belong in the design itself.

## Files, visibility, instantiation

* A file's top scope is setup code, run once. Only `pub` top-scope lambdas,
  types, and constants can be imported: `const lib = import("file")` /
  `import("file.pub_name")` / `import("proj/file")`. No glob patterns.
  `pub mut`, `pub reg` and `pub wire` are compile errors; the SYNTHESIZABLE
  cross-hierarchy register attach (`regref` by string path, zero-or-many
  matches) is TBD — design code reaches registers through the ordinary
  instance hierarchy instead.
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
// Counter — registered-output interface form: q IS the output, `= 0` resets it
mod counter2(enable:Bool) -> (reg count:U8@[0] = 0) {
  if enable { wrap count += 1 }      // conditional/feedback write: state, @[0]
}
// Counter — body register read by a combinational output
mod counter1(enable:Bool) -> (value:U8@[0]) {
  reg count:U8 = 0
  value = count
  if enable { wrap count += 1 }
}

// FSM (names are case-sensitive: `state` and type `State` coexist)
enum State = (Idle, Run, Done)

mod fsm(start:Bool, fin:Bool) -> (busy:Bool@[0]) {
  reg state:State = State.Idle
  busy = state == State.Run
  match state {
    == State.Idle { if start { state = State.Run  } }
    == State.Run  { if fin   { state = State.Done } }
    else          { state = State.Idle }
  }
}

// 1-cycle dual-port RAM
pipe[1] dpram(we:Bool, waddr:U8, raddr:U8, wdata:U32) -> (rdata:U32) {
  reg mem:[256]U32 = 0
  if we { mem[waddr] = wdata }
  rdata = mem[raddr]
}

// Generic width + clog2-sized select
mod pick<N=8>(v:Unsigned(bits=N), sel:Unsigned(bits=std.clog2(N))) -> (b:Bool@[0]) {
  b = v#[sel] == 1
}

// Two clock domains: two Clock inputs, so every register names its clock;
// the single Reset `rst` stays implicit (active-low only where negreset=true)
mod two_clk(clk_a:Clock, clk_b:Clock, rst:Reset, a:U8, b:U8) -> (qa:U8@[0], qb:U8@[0]) {
  reg ra:U8:[clock_pin=clk_a] = 0
  reg rb:U8:[clock_pin=clk_b, negreset=true] = 0
  if a != 0 { ra = a }
  if b != 0 { rb = b }
  qa = ra
  qb = rb
}
```

## Verilog ↔ Pyrope quick map

| Verilog | Pyrope |
|---------|--------|
| `module m(...)` | `mod m(...) -> (out:T@[N])` (or `pipe[N]`/`comb`) |
| `input [7:0] x` / `output [7:0] y` | `x:U8` input / `y:U8@[N]` mod output |
| `input clk, rst_n` | implicit clock/reset BY TYPE: `clk:Clock, rst_n:Reset` (omit at the call site); an active-low reset needs `negreset=true` on its registers |
| `reg [7:0] x` + reset | `reg x:U8 = 0` |
| `reg [7:0] x` / procedural blocking `=` | `mut x:U8 = 0` |
| `wire [7:0] x` / continuous `assign` | `wire x:U8 = ...` (single-driver net) |
| `x <= y` (non-blocking) | `x = y` on a `reg` (registered write) |
| `always @(posedge clk or posedge rst)` | `reg x:U8:[async=true] = 0` |
| `parameter N = 8` | `comptime const N = 8`, or a constant generic `<N=8>` |
| `logic [N-1:0] x` / `$clog2(N)` | `x:Unsigned(bits=N)` / `std.clog2(N)` |
| `always @(posedge clk)` / `@(*)` | implicit — `reg` vs `mut` |
| `case (x) ... endcase` | `match x { == v {...} else {...} }` |
| `x[6:3]` | `x#[3..=6]` |
| `y8 = ~x1` (context-widened `~`, 8'hFF for x1 == 0) | `y8 = ~U8(x1)` or `~x1#[0..<8]` — a plain `~x1` of a `U1` flips 1 bit |
| `{a, b}` concat | `(b, a)#[..]` — entry 0 lands in the LOW bits, so the argument order REVERSES |
| `4'b10x?` / `x` value | `0ub10??` / `0sb?` |
| one-hot mux / tri-state bus | `unique if` (lowers to `__hotmux`) |
| Verilog reg memory read semantics | `reg mem:[N]T:[ordering="old"]` |
| Verilog-style flops, no cycle checks | `mod m::[timecheck=false](...)` (escape hatch; the importer sets it) |
| SVA `$rose(req) \|-> ##[1:10] $rose(ack)` | `assert(rose(req) implies rose(ack, 1..=10))` (temporal lib TBD) |
| testbench `initial` | `test name.leaf { ... tick N { ... step ... } }` |

## Gotchas — check before emitting code

1. `return X` is always wrong — assign the named output, then bare `return`.
2. Outputs must be named in `-> (...)`; no positional returns; clause is
   mandatory (except `self` methods).
3. `match` is parallel, not priority: overlapping arms break the one-hot
   obligation. `else` is optional when arms cover the key space; use
   `if/elif` for priority.
4. `when`/`unless` trailing gates and `.[defer]` no longer exist — `if` blocks
   and `wire` nets respectively.
5. `@[N]` never inserts flops (pure check); `stage[N]` inserts them
   (`mod`-only). A `mod` output without `@[N]`/`@[]` is a compile error; a
   comb path through a `pipe` is illegal.
6. No Bool/integer mixing, also across ports and instance outputs: `if 5 {}` is a
   type error → `if 5 != 0 {}`; `U1(flag)` for Bool→bit, `Bool(x)` for
   bit→Bool. Reduce ops (`x#|[..]`) return integer 0/1, not Bool.
7. Narrowing assignments need `wrap`/`sat`; widths come from types, never from
   a `:[max=...]` attribute. The same rule binds arguments: a `U16` into
   `a:U4` is a compile error — pass `x#[0..<4]`.
8. Loop bounds must be comptime (loops unroll); `tick` (test-only) is the only
   runtime-count loop. No comprehensions. Never drive a `wire` inside a loop.
9. `0b1010` is invalid — `0ub1010`/`0sb1010`. No bare `?`/`_` initializers;
   use `nil` or `0sb?`. No `int`, no `x:i8` type and no `I8` type — `S<N>`/`Signed`. Type words
   are capitalized (`U8`, `Bool`); `u8`/`bool`/`s1` are ordinary names, but as a
   type or cast (`x:u8`, `u8(x)`) they are a "renamed `U8`" error.
10. `++` is tuple/string concat, never arithmetic. `#[]` bits vs `[]` elements
    vs `@[]` cycles.
11. Name your call arguments (`f(a=1, b=2)`); UFCS only on `self` lambdas;
    `ref` written at declaration and call.
12. The comptime `[...]` slot is a syntax error — comptime parameters are
    constant generics: `comb g<N=1>(x)`, called `g<N=3>(x=a)`.
13. `*_pin` takes no `ref` (`clock_pin=clk`); reset value is the `= expr`
    initializer; `async=true` for async reset (never `sync=`); `negreset=true`
    for active-low (a `_n` name means nothing).
14. Enum comparisons use names (`State.Idle`), never the underlying integer.
15. `if c { assert(x) }` checks `assert(x)` UNCONDITIONALLY — write
    `assert(c implies x)`.
16. Test names are dotted identifiers (`test add.basic`), never strings.
    Design-body asserts are not executed by `lhd sim` — put checked asserts in
    the `test` block.
17. Clocks/resets are TYPES: a register's clock is the single `Clock` input
    (`clk:Clock`), never an input merely NAMED `clk` (a `clk:U1` port is data
    and the module then mints `clock:Clock`). Never bind a constant to a
    `Clock` (`dut(clk=true, ...)` is an error); omit it and the caller's
    implicit clock is wired. A `Bool` input named `clk` may be constant.
18. A nested lambda reads only compile-time constants of its enclosing scopes
    (`comptime const`, a `const` that folds to a constant, generics) — pass
    runtime values as inputs. Lambda inputs/outputs/generics must not reuse a
    visible enclosing const/type/lambda name.
19. `~x` flips only an unsigned-typed x's OWN width: `x:U3` gives `7 - x`,
    also into a `U8` (5 for x == 2, not 253); `const t = w` is a U3 alias too;
    a signed x or a literal gives `-x - 1`; `~(w + 1)` (untyped runtime) is a
    compile error. Widen first for a wider flip (`~U8(x)`, `~x#[0..<8]`).

## Known LiveHD bugs (work around; re-test before trusting)

Re-verified 2026-09-28 (lhd built 2026-09-28) with the OLD lowercase type
spellings; the entries below are respelled with the 2026-09-29 capitalized type
words and were not re-run after that migration. **Fixed since 2026-09-27**, no
workaround needed any more: generic `comb` outputs with generic widths;
`Unsigned(bits=16 * N)` ports read in loops; false combinational loop through a
child's register (drop `::[timecheck=false]`); bit writes straight into an
output (`y#[i] = ...`); `f<N=x.[bits]>`; nested lambdas reading an enclosing
`comptime const`; `wire` reads and `reg`-array writes inside `for`; byte-enable
memory writes (`mem[a]#[r] = d`); `a#[0..+1]`; single-output auto-unwrap;
named tuple `type` ports, `.[bits]` of a tuple field, `U1()` of a `Bool` tuple
field, tuple outputs from a typed `comb`; stateful children instantiated inside
a `for` loop in `lhd sim`; an absent field of a one-named-field tuple or a
one-member import namespace (now `unknown-field`); a call omitting a defaulted
`mod`/`pipe` input; test blocks reading a file-scope `const` or an instance
output outside a `tick`. **Fixed 2026-09-29:** enum-typed ports/regs (one
signed bit before), LEAF values of hierarchical enums in hardware, `mod`/`pipe`
defaults that fold to a constant (generics, `comptime const`s, comb calls,
if-expressions, attribute reads, `const X = enum(...)` entries), untyped
outputs of a fully typed `mod` read into typed destinations (a Bool one stays
a Bool), the Pyrope writer on a `comb` default computed through calls; calls
into a `mod` imported pre-elaborated from an `ln:` dir (or restored from the
compile cache) now get the argument/output fit checks (they truncated
silently); `.[bits]` of a typed tuple field after an enum-entry write.

Still open:

* `lhd pyrope fmt` (default `--mode ai`: one line per statement, no width limit,
  sorts all-named call/tuple lists, rewrites `f(x=x)` to `f(x)`) can still
  change meaning: it sorts `type`/`enum` field declarations (positional
  initialization then binds differently) and shortens `q=q` even when the callee
  has no `q` (an error becomes a valid single-argument bind). LEC the formatted
  file against the unformatted one. (The compiler bugs where a bare same-name
  argument bound by position, for `inline=false` comb instances and for a
  same-kind leftover parameter, are fixed in livehd as of 2026-09-28.)

* **Silent:** a `reg` both bit-read and conditionally bit-written inside a
  rolled `for` loop (`out#[p] = r#[p]; if v#[p] == 1 { r#[p] = d#[p] }`) loses
  the write. Build the next value in a `mut` inside the loop and assign the
  register once after it, or `--set compile.unroll=true`.
* **Silent:** `reg mem:[N]T:[ordering="old"]` with several partial writes
  (`mem[a]#[r] = d`) to one entry in the same cycle keeps only the last one.
  Write the merged whole word.
* **Regression:** a memory element passed to a typed `comb` input
  (`f(x=mem[i])` with `x:U16`) → "(unbounded range) may not fit". Bind
  `const v:U16 = mem[i]` first.
* A PARENT value of a hierarchical enum (`Animal.bird`, not a leaf such as
  `Animal.bird.parrot`) does not lower to hardware yet: storing it in a typed
  local, comparing a port with it, or passing it to a port fails
  (assign-type-mismatch or an unresolved reference). Use leaf values.
* A registered output declared `@[1]` makes any caller that mixes it with
  same-cycle values fail ("mixes values at different cycles"), including the
  generated lhdtrack harnesses; on such tops declare it `@[]` with a comment.
* In a generic lambda that has any `mut`, `U1(<Bool input>)` failed
  ("undefined function 'u1'" before the type-word migration); cast a local
  const instead (`const b = in; U1(b)`).
* An import const named like its own file (`const pc = import(...)` in
  `pc.prp`) breaks generic calls through it; rename the const or the file.
* `lhd sim` may report a false dependency cycle ("occurrence-wide color
  scheduler") for one rolled loop computing both a request and the ready that
  comes back through another module; split the loop.
* Arrays with a generic element width: `reg r:[N]Unsigned(bits=N)` ("memory
  'r' element type must be a sized integer or bool") and array ports of a
  generic (`v:[N]Unsigned(bits=N)`, "has no declared type"). Pack into
  `Unsigned(bits=N*W)` and slice `#[(i*W)..+W]`.
* Bit-assign into an array element inside nested loops
  (`m[i]#[j] = ...`) → "array index is negative (range [-4, 3])". Build the
  row in a scalar local and assign `m[i] = row`.
* `std.clog2(x)` and value-derived `K.[bits]` (13.[bits] == 4) are documented
  but not in the compiler (`std` undefined; `.[bits]` of a comptime value is
  not its width). Pass widths as extra generics, or use
  `Unsigned(max=DEPTH - 1)` as an index type.
* An unparenthesized expression as a generic argument (`m<W=2*N - 1>`) does
  not parse; write `m<W=(2*N - 1)>`.
* Unverified since the fix pass: `lhd sim` reading a `wire` back-edge one
  cycle late inside a `for` loop (silent, LEC still proved it). Back loop
  refactors with a full-length simulation.
* An instance handle named like a `reg` in the body of a non-`pub` child
  (`const c = cnt8(...)` when `cnt8` declares `reg c`) fails ("call to
  undefined function 'cnt8'"); rename the handle.
* `U1()`/`Bool()` on a DESTRUCTURED or auto-unwrapped `Bool` output
  (`const (f, g) = child(...); U1(f)`) or on a `Bool` tuple field failed
  ("call to undefined function 'u1'" before the migration): read it through
  the instance, `const c = child(...); U1(c.f)`.
* A named tuple `type` as a `mod` port type, `.[bits]` on a tuple field, and
  a tuple output from a typed `comb` fail; use inline tuple port types and
  `mod`.
* The documented `pipe` state-output idiom `pipe[1] c(en:Bool) -> (reg
  count:U8) { if en { wrap count += 1 } }` fails ("state register 'count' …
  homes at stage 0 but its declared landing cycle 1 requires home 1"); use the
  `mod` form `-> (reg count:U8@[0] = 0)` (`counter2` pattern).

## Not yet implemented (avoid emitting)

Valid spec Pyrope that LiveHD does not lower yet (the `pyrope/15-tbd/` doc
chapter is the authoritative list; status below last re-checked 2026-09-27).
Do not generate these unless explicitly asked:

* `fluid` lambdas / valid-retry handshakes (parses only).
* The verification **temporal library** — `past(x, n)`, `rose(x [, w])`,
  `fell`, `stable`, `changed`, `eventually(x, w)`, `always(x, w)`. Cycle
  arguments are **positional** (there is no `f[N](x)` bracket form in this
  library) and windows are bounded ranges (`1..=10`). No
  `.[rising]`/`.[falling]`/`.[changed]` attributes. `lhd formal verify`
  rejects these with an explicit not-implemented diagnostic. (The *pipelining*
  `past[N](x)` DOES work — design body only.)
* Testbench extras: `force`/`release`, `cpp("model")` external models,
  unbounded `tick`; the SYNTHESIZABLE string-path `regref`; `assert.[failed]`.
  (The `test`-block `regref` works — see Tests.)
* `cover`/`covercase`; in-language `lec()`; `.[rand]`/`.[crand]` (rejected in
  test blocks and design bodies; survive only where they constant-fold).
* `macro=` memory-compiler binding; `import("prp")` stdlib (the built-in
  `std` namespace with `std.clog2` needs no import); the `retime` register
  attribute (parses, warns, not lowered).
* The landing-cycle rule for a BODY `reg` driving a `mod` output is unsettled
  (see Registers and timing).
* `format(...)`, operator-overload hooks (`eq`/`lt`/`to_string`/`to_bool`),
  strings as char tuples, `:Param_type(String)`, `u(W)`, recursive-enum ADTs,
  `pub wire`, `sigref`/`peek`/`poke`, the tuple-LHS `in` subset test and the
  binary `a ~& b` / `a ~| b` / `a ~^ b` spellings were all REMOVED (or never
  existed) — do not emit them; write `~(a & b)` / `~(a | b)` / `~(a ^ b)`.

Note: generic constant/lambda bindings, generic defaults, named `<T=…>`
bindings, body references of a generic, and input default values **all work**;
prefer trusting `lhd` over any TBD table.

Checking a comptime-only `.prp` (no `pub mod`/`pub comb` hardware entity) needs
`--set upass.tolg=false --set upass.verifier=true`. Bare `lhd compile` forces
lowering and invents `tolg-error: unresolved reference '%self_0'` /
`tuple-store-unsupported` on programs that contain no hardware.

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
lhd compile ln:foo_lns/ --emit net.v           #   valid INPUTS (compile/sim/lec/synth)
lhd sim foo.prp                       # run every test block in the file
lhd sim foo.prp add.basic --arg n=4   # one test (dotted selector), runtime args
lhd formal verify foo.prp props.verify.prp --top foo --set formal.bound=12
lhd lec --impl foo.prp --ref gold.v --top gold --set formal.solver=cvc5
lhd synth foo.prp --top foo --workdir W --stats  # compile -> color synth -> map -> STA
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
  `simfail_*.prp`, VCD). Knobs: `--set formal.bound/timeout/engine/...`;
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
* LEC verdicts: `proven`/`refuted` are answers; `unknown` (timeout or
  inconclusive) is neither — it has hidden real miscompiles, so back it with
  simulation. For refactors, LEC each small step against the *previous*
  version (compile each side with `--set compile.upass.inline=false
  --emit-dir lg:DIR`), not against the original: small deltas prove in
  seconds. Renamed instances break flop pairing (`const arb = child(...)`
  names the instance `arb`); pair them with
  `--set formal.lec.match='ref.inst.q=impl.inst.q'` (equal widths only).
* **`lhd synth`** is the one-shot synthesis flow (compile -> `pass color
  synth` -> mapper -> OpenTimer STA) over one in-memory design. `--top` takes
  the bare entity; `--set synth.mapper=abc` (default) | `usyn`; one Liberty
  (`--set synth.liberty=…`, default
  `$HAGENT_TECH_DIR/sky130_fd_sc_hd__tt_025C_1v80.lib`) feeds mapping and STA.
  With `--workdir W` the compiled design, mapped netlist and reports land in
  `W/synth/` and a re-run reuses everything unchanged
  (`--set lhd.incremental=false` = honest cold run). Outputs: `--emit
  verilog:PATH` / `--emit-dir lg:` (netlist), `--emit-dir report:`; `--stats`
  adds per-region rows; pass knobs ride their namespace (`--set
  abc.adder=cla`). Check `lhd help synth` for the current knobs.
* Cleaning machine-emitted Pyrope (e.g. `lhd compile ... --emit-dir pyrope:`)
  into idiomatic code: follow `livehd/docs/sample_prompt_cleanup_pyrope.md`.
* `lhd pyrope style` suggests loops/bundles for repeated code in
  machine-emitted Pyrope; `lhd pyrope fmt -i` formats (re-check long
  if-expressions and argument lists it joins onto one line).

Triage diagnostics by `category`: `syntax`/`name`/`type`/`bitwidth` — the
source is wrong, fix it; `unsupported` — valid Pyrope LiveHD cannot lower yet
(see the TBD list), rewrite around it, do not "fix" correct source;
`internal` — a LiveHD bug: reduce to a repro, do not change the source.
EXCEPTION: timing/landing-cycle ("lands at cycle(s) … but … is declared"),
combinational-loop and clock/reset-ambiguity errors currently print as
`error[internal]` although they are SOURCE errors — fix the source. The
frontend is more permissive than the spec (stale forms may still parse), so
passing `lhd compile` is necessary but not sufficient — follow this skill's
rules for style/semantics.
