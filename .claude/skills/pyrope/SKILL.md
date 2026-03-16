# Pyrope Skills Guide for Coding Agents

A practical reference for generating valid Pyrope code or translating between
Pyrope and Verilog. This guide assumes familiarity with hardware design concepts.

## Quick Reference

### Storage Classes
```pyrope
comptime SIZE = 16          // Compile-time constant (shorthand for comptime const)
comptime mut idx = 0        // Mutable at elaboration time
const val = input           // Immutable after assignment (runtime, NOT compile-time)
mut wire = 0                // Combinational wire (reassignable, no persistence)
reg state = 0               // Register (persistent across cycles)
```

### Lambda Types
```pyrope
comb add(a, b) { a + b }                    // Pure combinational, no state
pipe[3] mul(a, b) -> (c) { c = a * b }      // Moore machine, 3-cycle fixed latency
pipe[1..=3] flex(a) -> (c) { c = a }        // Compiler chooses 1-3 cycles
pipe arb(a, b) -> (c) { c = a + b }         // Bare: caller specifies latency via delay[N]
flow top(a, b) -> (out) { /* timing */ }     // Explicit pipeline timing with @[N]
mod counter(en) -> (val) { /* regs */ }      // No constraints, Mealy or Moore
```

### Operators
```pyrope
// Arithmetic: +  -  *  /  %  (% is debug-only)
// Bitwise:    &  |  ^  ~
// Logical:    and  or  !  not  implies
// Comparison: ==  !=  <  <=  >  >=  (chained: a <= b <= c)
// Shifts:     <<  >>
// Concat:     ++  (tuple concatenation — strings, lambdas, tuples)
// Bit select:  val#[3..=6]   val#[3]   (NEVER use #[] for timing)
// Timing:      val@[0]  val@[-1]  val@[]  (NEVER use @[] for bit select)
```

### Timing Syntax (registers and flow)
```pyrope
reg counter:u8 = 0
counter@[0]     // Current value (same as just 'counter')
counter@[]      // Deferred value (end of current cycle, same as din)
counter@[-1]    // Previous cycle value
counter@[1]     // Next cycle value (debug/assert only)
```

### Flow Timing (three mechanisms)
```pyrope
flow example(a, b) -> (out) {
  const tmp = delay[3] mul(a@[0], b@[0])   // delay[N]: operation takes N cycles
  const aligned = delay[3] a@[0]            // var@[N]: use value at cycle N
  out:@[4] = delay[1] add(tmp@[3], aligned@[3])  // :@[N]: timing type check (optional)
}
```

### Control Flow
```pyrope
if cond { a } else { b }                  // Standard conditional (creates new scope)
unique if c1 { x } elif c2 { y } else { z }  // Mutually exclusive (can optimize to tri-state)
match val { == 0 { a } < 5 { b } else { c } }  // Always unique, any comparison operator
a += 1 when enable                         // Trailing conditional (no new scope)
return 0 unless valid                      // Early exit (return only for early exits)
for i in 0..=7 { mem[i] = 0 }             // Bounds must be comptime (unrolled)
```

### Assertions and Verification
```pyrope
assert cond                    // Runtime check (skipped during reset/invalid)
cassert cond                   // Must be verified at compile time
optimize cond                  // Assert + allows synthesis optimization
always assert cond             // Checked even during reset
cover cond, "message"          // Must be true at least once during testing
test "name" { step; assert x } // Test block (step advances one clock cycle)
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

### FSM
```pyrope
enum State = (Idle, Run, Done)

mod fsm(start:bool, fin:bool) -> (busy:bool) {
  reg state:State = State.Idle

  busy = state == State.Run

  match state {
    case State.Idle { state = State.Run when start }
    case State.Run  { state = State.Done when fin }
    case State.Done { state = State.Idle }
  }
}
```

### Pipeline (multiply-add)
```pyrope
pipe mul(a, b) -> (c) { c = a * b }
pipe add(a, b) -> (c) { c = a + b }

flow multiply_add(in1, in2) -> (out) {
  const tmp = delay[3] mul(in1@[0], in2@[0])
  const in1_d = delay[3] in1@[0]
  out:@[4] = delay[1] add(tmp@[3], in1_d@[3])
}
```

### Memory
```pyrope
mod mem_block(we:bool, addr:u8, wdata:u32) -> (rdata:u32) {
  reg memory:[256]u32 = 0
  rdata = memory[addr]
  memory[addr] = wdata when we
}
```

### Tuple with getter/setter
```pyrope
mut saturating_counter = (
  mut _val:u8 = 0,
  getter = comb(self) { self._val },
  setter = comb(ref self, v:u8) { self._val::[saturate] = v }
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

    // If custom clock/reset needed:
    mod counter2(enable:bool) -> (
      reg count:u8:[reset_pin=ref rst, clock_pin=ref clk] = 0
    ) {
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
        case State.Idle   { state = State.Run when start }
        case State.Run    { state = State.Finish when done }
        case State.Finish { state = State.Idle }
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

      acc = if clear { 0 } else { acc + din }
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

### Example 2: Pipeline with Flow

=== "Pyrope"
    ```pyrope
    pipe mul(a:u16, b:u16) -> (c:u32) { c = a * b }
    pipe add(a:u32, b:u32) -> (c:u32) { c = a + b }

    flow mac(a:u16, b:u16, c:u16) -> (out:u32) {
      const prod = delay[3] mul(a@[0], b@[0])
      const c_d  = delay[3] c@[0]
      out:@[4] = delay[1] add(prod@[3], c_d@[3])
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
| `always @(posedge clk)` | (implicit — registers update at cycle boundary) |
| `always @(*)` | (implicit — combinational logic is default) |
| `if/else` | `if/else` |
| `case(x)` | `match x { case ... }` |
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
| `mod f(...)` | Standard `module` |
| `flow f(...)` | Module instantiating sub-modules with delay alignment |
| `reg x:u8 = 0` | `reg [7:0] x` with reset to 0 |
| `mut x:u8 = ?` | `wire [7:0] x` or `reg` in combinational always block |
| `const x = expr` | `wire` with `assign` |
| `comptime N = 8` | `parameter N = 8` or `localparam` |
| `x += 1 when en` | `if (en) x <= x + 1;` |
| `match x { ... }` | `case(x) ... endcase` |
| `enum State = (A, B, C)` | `localparam A=3'b001, B=3'b010, C=3'b100;` (one-hot) |
| `x#[3..=6]` | `x[6:3]` |
| `assert cond` | `assert(cond)` or SVA |
| `test "name" { ... }` | `initial begin ... end` in testbench |
| `step` | `@(posedge clk);` |
| `x@[0]` | Current register output `q` |
| `x@[]` | Register input `d` (deferred write) |

## Key Differences from Verilog

1. **No blocking vs non-blocking ambiguity**: Registers automatically defer
   updates. `reg x = 0; x = x + 1` does the right thing.

2. **No sensitivity lists**: Combinational logic is implicit. No `always @(*)`.

3. **No `wire`/`reg` confusion**: `mut` = combinational, `reg` = sequential. Clear.

4. **Structural typing**: No need to declare port widths in module instantiation.
   Types are checked structurally.

5. **Everything is a tuple**: Modules return tuples. Multi-output is natural.

6. **Compile-time loops**: `for` is unrolled at elaboration. No runtime loops.

7. **No tri-state `z`**: Use `unique if` instead. EDA tools can optimize to
   tri-state buffers.

8. **`#[]` vs `@[]`**: Bit selection uses `#[]`, timing uses `@[]`. Never mix them.

9. **`?` vs `nil`**: `?` is unknown (valid, Verilog `x`). `nil` is invalid
   (assertion error if used, must be eliminated at compile time).

10. **`ref` is semantic, not performance**: In hardware all signals are wires.
    `ref` allows mutation, not optimization.
