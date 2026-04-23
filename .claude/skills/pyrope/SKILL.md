# Pyrope Skills Guide for Coding Agents

<!--
  This guide targets **Pyrope 3.0** (the implementation under `inou/prp` in
  LiveHD). Pyrope 3.0 is syntactically and semantically distinct from
  Pyrope 2.0 (under `inou/pyrope`): lambda kinds, pipelining timing,
  register access, and attribute syntax are NOT compatible. Do not mix
  examples from 2.0 sources — prefer the specs under
  `docs/docs/pyrope/00-hwdesign.md` through `10-internals.md`.
-->

A practical reference for generating valid Pyrope 3.0 code or translating
between Pyrope and Verilog. This guide assumes familiarity with hardware
design concepts.

## Quick Reference

### Storage Classes
```pyrope
comptime SIZE = 16          // Compile-time constant (shorthand for comptime const)
comptime const SIZE2 = 16   // Explicit form of above
comptime mut idx = 0        // Mutable at elaboration time
const val = input           // Immutable after assignment (runtime, NOT compile-time)
mut wire = 0                // Combinational wire (reassignable, no persistence)
reg state = 0               // Register (persistent across cycles)
```

Uppercase identifiers are implicitly `comptime const` by convention.

### Lambda Types (only three)
```pyrope
comb add(a, b) -> (result) { result = a + b }    // Pure combinational, no state
pipe[3] mul(a, b) -> (c) { c = a * b }           // Moore machine, fixed 3-cycle latency
pipe[1..=3] flex(a) -> (c) { c = a }             // Compiler/caller picks within range
pipe mul2(a, b) -> (c) { c = a * b }             // Bare: caller picks latency via await[N]
mod counter(en) -> (val) { /* regs + orchestration */ }   // No constraints; can be Mealy, Moore, or a pipeline orchestrator
```

`mod` replaces the old `flow` lambda kind — pipeline orchestration lives
inside `mod` now, using `await[N]` and `:@[N]`.

A `comb`/`pipe`/`mod` with `self` as the first argument is a **method**.
`async` is reserved for future use.

### Operators
```pyrope
// Arithmetic: +  -  *  /  %  (% is debug-only)
// Bitwise:    &  |  ^  ~   ~&  ~|  ~^   (NAND/NOR/XNOR)
// Logical:    and  or  !  not  implies  !and  !or  !implies
// Comparison: ==  !=  <  <=  >  >=   (chained: a <= b <= c)
// Shifts:     <<  >>
// Concat:     ++  (tuple concatenation — strings, lambdas, tuples)
// Bit select: val#[3..=6]   val#[3]           (NEVER use #[] for timing)
// Cycle type: val:@[N]  on LHS or RHS         (pure type check; NEVER use @[] for bit select)
// Timing:     await[N] lhs = rhs              (declaration modifier in mod only)
// Attribute:  var::[attr]  var::[attr=value]  (reads/writes attribute)
```

### Register and pipeline timing (Pyrope 3.0)

Registers are read/written through **bare references**, `::[defer]`, and
`past[n]()` — the older `@[0]`, `@[]`, `@[-1]`, `@[1]` forms from Pyrope 2.0
are **not valid** in Pyrope 3.0.

```pyrope
reg counter:u8 = 0

const q_now   = counter            // current 'q' value
const q_end   = counter::[defer]   // end-of-cycle value (next cycle's 'q')
counter       = counter + 1        // write: takes effect at cycle boundary (like Verilog <=)
counter::[defer] = 42              // explicit deferred write (end-of-cycle)

const prev1   = past[1](counter)   // value from 1 cycle ago (debug; inserts a flop)
const prev2   = past[2](counter)   // value from 2 cycles ago
const prev    = past(counter)      // shorthand for past[1](counter)
```

### Pipeline timing inside `mod` (three constructs)
```pyrope
pipe mul(a, b) -> (c) { c = a * b }
pipe add(a, b) -> (c) { c = a + b }

mod mac(a, b, c) -> (out) {
  await[3] tmp      = mul(a, b)               // await[N]: RHS is delivered N cycles later
  await[3] c_d      = c                       // await[N] on any RHS, not just a pipe call
  await[1] out:@[4] = add(tmp:@[3], c_d:@[3]) // :@[N] on use asserts value lives at cycle N
}
```

Rules:
* `await[N]` is a **declaration modifier** (in the same slot as `const`/`mut`/`reg`)
  and is only valid inside `mod`. Not allowed in `comb` or `pipe` bodies.
* `:@[N]` is a **pure cycle type check** on either LHS or RHS uses —
  it inserts no flops; misalignment is a compile error.
* Bare `pipe` calls **must** be consumed via `await[N]` at the call site.

### Control Flow
```pyrope
if cond { a } else { b }                         // Standard conditional (creates new scope)
unique if c1 { x } elif c2 { y } else { z }      // Mutually exclusive (generates __hotmux; may optimize to tri-state)
match val {
  == 0     { a }        // implicit == allowed when entry is bare expr
  < 5      { b }
  in 6,7,8 { c }
  else     { d }
}
a += 1 when enable                                // Trailing conditional (no new scope)
return 0 unless valid                             // Early exit (return only for early exits)
for i in 0..=7 { mem[i] = 0 }                    // Bounds must be comptime (unrolled)
while cond { ... }                                // Unrolled at elaboration
loop { ... }                                      // == while true
break  continue                                   // Works in for / while / loop
```

`match` must have exactly one matching branch; if none match and there is no
`else`, an error is generated (it behaves like a unique parallel case with an
implicit `optimize`). The last expression in a block is the implicit return
value; `return` is only for early exits.

### Assertions and Verification
```pyrope
assert cond                    // Runtime check (skipped during reset/invalid)
cassert cond                   // Must be verified at compile time
optimize cond                  // Assert + allows synthesis optimization
always assert cond             // Checked even during reset/invalid
always cassert cond            // Compile-time variant that ignores reset/valid gate
always optimize cond           // Same, for optimize
cover cond, "message"          // Must be true at least once during testing
covercase grp, cond, "msg"     // Coverage grouped with `grp`
requires cond                  // Lambda precondition (allows optimizer to assume it)
ensures  cond                  // Lambda postcondition (same)
test "name" { step; assert x } // Test block; `step` advances one clock cycle
```

### Test-only primitives (from `09-verification.md`)
```pyrope
step              // advance one cycle
step 5            // advance five cycles
waitfor cond      // block until cond becomes true (can use ::[rising], ::[falling], ::[changed])
poke  "path", v   // drive a signal in the DUT hierarchy
sigref("path")    // reference a DUT signal by hierarchical path
regref("path")    // reference a DUT register by hierarchical path
spawn name = { ... }  // concurrent stimulus/monitor thread; `cancel name` to kill
```

## Common Patterns

### Counter
```pyrope
mod counter(enable:bool) -> (value:u8) {
  reg count:u8:[wrap] = 0
  value = count
  count += 1 when enable
}
```

Or with registered output:
```pyrope
mod counter(enable:bool) -> (reg count:u8 = 0) {
  count += 1 when enable
}
```

Custom clock/reset pins:
```pyrope
mod counter2(enable:bool) -> (
  reg count:u8:[wrap=true, reset_pin=ref rst, clock_pin=ref clk] = 0
) {
  count += 1 when enable
}
```

### FSM
```pyrope
enum State = (Idle, Run, Done)

mod fsm(start:bool, fin:bool) -> (busy:bool) {
  reg state:State = State.Idle

  busy = state == State.Run

  match state {
    == State.Idle { state = State.Run  when start }
    == State.Run  { state = State.Done when fin   }
    == State.Done { state = State.Idle            }
  }
}
```

### Pipeline (multiply-add)
```pyrope
pipe mul(a, b) -> (c) { c = a * b }
pipe add(a, b) -> (c) { c = a + b }

mod multiply_add(in1, in2) -> (out) {
  await[3] tmp      = mul(in1, in2)
  await[3] in1_d    = in1
  await[1] out:@[4] = add(tmp:@[3], in1_d:@[3])
}
```

### Accumulator over a pipelined multiplier
```pyrope
mod accum(in1, in2) -> (out) {
  reg total = 0
  await[3] tmp = mul(in1, in2)
  total::[defer] = total + tmp:@[3]
  out = total
}
```

### Async memory
```pyrope
mod mem_block(we:bool, addr:u8, wdata:u32) -> (rdata:u32) {
  reg memory:[256]u32 = 0
  rdata = memory[addr]
  memory[addr] = wdata when we
}
```

### Dual-port RAM (1-cycle read)
```pyrope
pipe[1] dual_port_ram(we:bool, waddr:u8, raddr:u8, wdata:u32) -> (rdata:u32) {
  reg mem:[256]u32 = 0

  mem[waddr] = wdata when we
  rdata = mem[raddr]
}
```

### Tuple with getter/setter
```pyrope
mut saturating_counter = (
  mut _val:u8 = 0,
  getter = comb(self) { self._val },
  setter = comb(ref self, v:u8) { self._val::[saturate] = v },
)

saturating_counter = 300     // saturates to 255
assert saturating_counter == 255
```

## Verilog to Pyrope Translation

### Example 1: Simple Counter

=== "Verilog"
    ```verilog
    module counter #(parameter WIDTH = 8) (
      input  wire             clk,
      input  wire             rst,
      input  wire             enable,
      output reg  [WIDTH-1:0] count
    );
      always @(posedge clk) begin
        if (rst)
          count <= 0;
        else if (enable)
          count <= count + 1;
      end
    endmodule
    ```

=== "Pyrope"
    ```pyrope
    mod counter(enable:bool) -> (reg count:u8 = 0) {
      count += 1 when enable
    }
    ```

### Example 2: Mux with Priority

=== "Verilog"
    ```verilog
    module priority_mux (
      input  wire [7:0] a, b, c,
      input  wire [1:0] sel,
      output reg  [7:0] out
    );
      always @(*) begin
        case (sel)
          2'b00: out = a;
          2'b01: out = b;
          2'b10: out = c;
          default: out = 8'h00;
        endcase
      end
    endmodule
    ```

=== "Pyrope"
    ```pyrope
    comb priority_mux(a:u8, b:u8, c:u8, sel:u2) -> (out:u8) {
      out = match sel {
        == 0 { a }
        == 1 { b }
        == 2 { c }
        else { 0 }
      }
    }
    ```

### Example 3: FSM

=== "Verilog"
    ```verilog
    module fsm (
      input  wire clk, rst, start, done,
      output reg  busy,
      output reg  [1:0] state
    );
      localparam IDLE = 2'b01, RUN = 2'b10, FINISH = 2'b100;

      always @(posedge clk) begin
        if (rst) begin
          state <= IDLE;
        end else begin
          case (state)
            IDLE:   if (start) state <= RUN;
            RUN:    if (done)  state <= FINISH;
            FINISH: state <= IDLE;
            default: state <= IDLE;
          endcase
        end
      end

      assign busy = (state == RUN);
    endmodule
    ```

=== "Pyrope"
    ```pyrope
    enum State = (Idle, Run, Finish)

    mod fsm(start:bool, done:bool) -> (busy:bool) {
      reg state:State = State.Idle

      busy = state == State.Run

      match state {
        == State.Idle   { state = State.Run    when start }
        == State.Run    { state = State.Finish when done  }
        == State.Finish { state = State.Idle              }
      }
    }
    ```

### Example 4: Shift Register

=== "Verilog"
    ```verilog
    module shift_reg #(parameter DEPTH = 4) (
      input  wire       clk, rst,
      input  wire [7:0] din,
      output wire [7:0] dout
    );
      reg [7:0] stage [0:DEPTH-1];
      integer i;

      always @(posedge clk) begin
        if (rst) begin
          for (i = 0; i < DEPTH; i = i + 1)
            stage[i] <= 8'b0;
        end else begin
          stage[0] <= din;
          for (i = 1; i < DEPTH; i = i + 1)
            stage[i] <= stage[i-1];
        end
      end

      assign dout = stage[DEPTH-1];
    endmodule
    ```

=== "Pyrope"
    ```pyrope
    mod shift_reg(din:u8) -> (dout:u8) {
      comptime DEPTH = 4
      reg stage:[DEPTH]u8 = 0

      stage[0] = din
      for i in 1..<DEPTH {
        stage[i] = stage[i-1]
      }

      dout = stage[DEPTH-1]
    }
    ```

### Example 5: Dual-Port RAM

=== "Verilog"
    ```verilog
    module dual_port_ram #(
      parameter DEPTH = 256,
      parameter WIDTH = 32
    ) (
      input  wire                    clk,
      input  wire                    we,
      input  wire [$clog2(DEPTH)-1:0] waddr, raddr,
      input  wire [WIDTH-1:0]        wdata,
      output reg  [WIDTH-1:0]        rdata
    );
      reg [WIDTH-1:0] mem [0:DEPTH-1];

      always @(posedge clk) begin
        if (we)
          mem[waddr] <= wdata;
        rdata <= mem[raddr];
      end
    endmodule
    ```

=== "Pyrope"
    ```pyrope
    pipe[1] dual_port_ram(we:bool, waddr:u8, raddr:u8, wdata:u32) -> (rdata:u32) {
      reg mem:[256]u32 = 0

      mem[waddr] = wdata when we
      rdata = mem[raddr]
    }
    ```

## Pyrope to Verilog Translation

### Example 1: Accumulator

=== "Pyrope"
    ```pyrope
    mod accumulator(din:u16, clear:bool) -> (total:u32) {
      reg acc:u32 = 0

      acc   = if clear { 0 } else { acc + din }
      total = acc
    }
    ```

=== "Verilog"
    ```verilog
    module accumulator (
      input  wire        clk, rst,
      input  wire [15:0] din,
      input  wire        clear,
      output wire [31:0] total
    );
      reg [31:0] acc;

      always @(posedge clk) begin
        if (rst)
          acc <= 32'b0;
        else if (clear)
          acc <= 32'b0;
        else
          acc <= acc + {16'b0, din};
      end

      assign total = acc;
    endmodule
    ```

### Example 2: Pipeline orchestrated by `mod`

=== "Pyrope"
    ```pyrope
    pipe mul(a:u16, b:u16) -> (c:u32) { c = a * b }
    pipe add(a:u32, b:u32) -> (c:u32) { c = a + b }

    mod mac(a:u16, b:u16, c:u16) -> (out:u32) {
      await[3] prod     = mul(a, b)
      await[3] c_d      = c
      await[1] out:@[4] = add(prod:@[3], c_d:@[3])
    }
    ```

=== "Verilog"
    ```verilog
    // mul: 3-stage pipelined multiplier
    module mul (
      input  wire        clk,
      input  wire [15:0] a, b,
      output reg  [31:0] c
    );
      reg [31:0] pipe1, pipe2;
      always @(posedge clk) begin
        pipe1 <= a * b;
        pipe2 <= pipe1;
        c     <= pipe2;
      end
    endmodule

    // add: 1-stage pipelined adder
    module add (
      input  wire        clk,
      input  wire [31:0] a, b,
      output reg  [31:0] c
    );
      always @(posedge clk) begin
        c <= a + b;
      end
    endmodule

    // mac: multiply-accumulate with explicit delay alignment
    module mac (
      input  wire        clk,
      input  wire [15:0] a, b, c,
      output wire [31:0] out
    );
      wire [31:0] prod;
      reg  [15:0] c_d1, c_d2, c_d3;

      mul u_mul(.clk(clk), .a(a), .b(b), .c(prod));

      // Delay c by 3 cycles to align with mul output
      always @(posedge clk) begin
        c_d1 <= c;
        c_d2 <= c_d1;
        c_d3 <= c_d2;
      end

      add u_add(.clk(clk), .a(prod), .b({16'b0, c_d3}), .c(out));
    endmodule
    ```

## Translation Rules

### Verilog to Pyrope

| Verilog | Pyrope |
|---------|--------|
| `module name(...)` | `mod name(...)` or `pipe[N] name(...)` |
| `input wire [N:0] x` | `x:uN` (in lambda arguments) |
| `output reg [N:0] y` | `reg y:uN` (in lambda outputs) |
| `output wire [N:0] y` | `y:uN` (in lambda outputs) |
| `reg [N:0] x` | `reg x:uN = 0` |
| `wire [N:0] x` | `mut x:uN = ?` |
| `parameter N = 8` | `comptime N = 8` |
| `localparam` | `comptime` |
| `assign x = expr` | `x = expr` |
| `always @(posedge clk)` | implicit — registers update at cycle boundary |
| `always @(*)` | implicit — combinational logic is default |
| `if/else` | `if/else` |
| `case(x)` | `match x { == v { ... } ... }` |
| `x[N:M]` | `x#[M..=N]` (bit select) |
| `x[i]` | `x[i]` (array element) |
| `{a, b}` | `(a, b)#[..]` (bit concatenation via tuple) |
| `x <= y` (non-blocking) | `x = y` (registers defer automatically) |
| `x = y` (blocking) | `mut x = y` |
| `$display(...)` | `puts ...` |
| `assert(cond)` | `assert cond` |

### Pyrope to Verilog

| Pyrope | Verilog |
|--------|---------|
| `comb f(a, b) { a + b }` | Combinational `module` or `function` |
| `pipe[N] f(...)` | Module with N pipeline registers on outputs |
| `mod f(...)` | Standard `module` (may orchestrate sub-modules with delay alignment) |
| `reg x:u8 = 0` | `reg [7:0] x` with reset to 0 |
| `mut x:u8 = ?` | `wire [7:0] x` or `reg` in combinational `always` block |
| `const x = expr` | `wire` with `assign` |
| `comptime N = 8` | `parameter N = 8` or `localparam` |
| `x += 1 when en` | `if (en) x <= x + 1;` |
| `match x { ... }` | `case(x) ... endcase` |
| `enum State = (A, B, C)` | `localparam A=3'b001, B=3'b010, C=3'b100;` (one-hot) |
| `x#[3..=6]` | `x[6:3]` |
| `assert cond` | `assert(cond)` or SVA |
| `test "name" { ... }` | `initial begin ... end` in testbench |
| `step` | `@(posedge clk);` |
| bare `counter` (read) | register output `q` |
| `counter::[defer]` | register input `d` (end-of-cycle value) |
| `past[n](counter)` | N chained flops feeding a debug signal |
| `await[N] x = rhs` | N-deep shift register on `rhs` |
| `x:@[N]` (use) | no-op Verilog (timing type check only; fails at compile if wrong) |

## Key Differences from Verilog

1. **No blocking vs non-blocking ambiguity**: Registers automatically defer
   updates. `reg x = 0; x = x + 1` does the right thing.

2. **No sensitivity lists**: Combinational logic is implicit. No `always @(*)`.

3. **No `wire`/`reg` confusion**: `mut` = combinational, `reg` = sequential.
   Clear.

4. **Structural typing**: No need to declare port widths in module
   instantiation. Types are checked structurally.

5. **Everything is a tuple**: Modules return tuples. Multi-output is natural.

6. **Compile-time loops**: `for` / `while` / `loop` are unrolled at
   elaboration. No runtime loops.

7. **No tri-state `z`**: Use `unique if` instead. EDA tools can optimize to
   tri-state buffers when branches are mutually exclusive.

8. **`#[]` vs `:@[]`**: Bit selection uses `#[]`, cycle timing uses `:@[N]`
   (type check) and `await[N]` (declaration). Never mix them.

9. **`0sb?` / `0b?` vs `nil`**: unknown bits live *inside an integer literal*
   (`0b?`, `0sb?`, `0b??10`, etc.), not as a bare `?`. They are valid
   integer values (Verilog `x`) and propagate through arithmetic
   (`0sb? + 1 == 0sb??`, `0sb? | 1 == 1`). `nil` is *invalid* — any
   arithmetic or branch on it is a hard error, and the compiler must
   prove it is eliminated before synthesis. Bare `?` is only a
   declaration placeholder (`mut x:u8 = ?` = "use the type default"),
   **not** an integer value you can do math on.

10. **`ref` is semantic, not performance**: In hardware all signals are
    wires. `ref` allows mutation, not optimization.

11. **Pipeline orchestration lives in `mod`**: There is no `flow` lambda in
    Pyrope 3.0. Use `mod` with `await[N]` (declare) and `:@[N]` (type-check).

12. **Register access is by name, not by cycle index**: bare `counter`
    reads `q`, `counter::[defer]` reads/writes end-of-cycle, `past[n]()` is
    debug-only read of a previous cycle. The Pyrope 2.0 forms `@[0]`,
    `@[-1]`, `@[1]`, bare `@[]` do not exist in 3.0.

## Gotchas — common mistakes

1. **Don't instantiate a module inside `if`/`match`.** Instantiate in the
   top scope and mux the result instead. Conditional instantiation is not
   allowed.

2. **`await[N]` only in `mod`.** Inside `comb` or `pipe` bodies it is a
   compile error. A bare `pipe` call *must* be consumed by an `await[N]`.

3. **`:@[N]` never inserts flops.** It is a type check. If the operand
   doesn't live at cycle `N`, the compiler errors out — it will not
   silently insert registers. Use `await[N]` to actually add cycles.

4. **`past[n]()` is debug-only.** It inserts flops whose sole purpose is
   feeding assertions/cover points; do not rely on it for synthesizable
   design logic.

5. **No variable shadowing.** Declaring `mut x` / `const x` / `reg x` in an
   inner scope when `x` is already visible is a compile error (tuples are
   the one exception — `self` scopes you into the tuple).

6. **`match` must be exhaustive.** If no case matches and there is no
   `else`, an error is generated. Add `else { assert false }` if you
   really want to forbid the default.

7. **`if/else` with no `else` does not have a default value** — it is a
   statement form. For expression form, always provide `else`.

8. **`_pin` attributes need `ref`.** `clock_pin=ref clk`, not
   `clock_pin=clk`.

9. **`nil` vs `0sb?` vs bare `?`.** For a don't-care / Verilog-`x`
   *integer value*, use the bit-literal forms `0sb?` (signed) or `0b?`
   (unsigned) — bare `?` is **not** an integer and does not propagate
   through arithmetic. Bare `?` is only a declaration placeholder
   (`mut x:u8 = ?` = "use the type's default"), so `? + 1` is a type
   error while `0sb? + 1` is `0sb??`. Use `nil` to mean *no value here
   yet*; reading a `nil` value is a hard error. The three are not
   interchangeable.

10. **No implicit int/bool conversion.** `if 5 { ... }` is a type error.
    Write `if 5 != 0 { ... }`.

11. **`++` is tuple/string concat, not arithmetic or bitwise.** Use `+`,
    `|`, `&` for those. Bit concatenation of raw values is `(a, b)#[..]`.

12. **`for`/`while`/`loop` bounds are comptime.** Data-dependent bounds
    are a compile error — the loop must fully unroll during elaboration.

13. **`%` (modulo) is debug-only.** Synthesizable code may not use it.

14. **Lambda declarations are anonymous.** Pyrope has no global function
    namespace — bind with `const`/`comb name = ...` and rely on scope.

15. **Initialization is explicit.** Every `mut`/`const`/`reg` declaration
    needs an initializer. Pick by intent:
    * a concrete value (`0`, `false`, `""`, `(a=1, b=2)`) — normal case.
    * `0sb?` / `0b?` (or any bit-literal with `?` digits) — an integer
      value with unknown bits that participates in arithmetic.
    * bare `?` — "use the type's default" placeholder (`mut x:u8 = ?`).
      Not a value; cannot be used in arithmetic.
    * `nil` — "no valid value yet" (tuple/range default). Reading it is
      a hard error; the compiler must prove it is gone before synthesis.
    Bare `=` without a value is not allowed.
