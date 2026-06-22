// Golden for runtime `wrap` of a subtraction narrowed into u32: 32-bit modular
// subtraction. `wrap d = x#[0..=31] - y#[0..=31]` keeps the low 32 bits of the
// (signed-capable) difference, which is exactly Verilog's `x - y` for 32-bit
// unsigned operands.
module \rt_wrap_sub.rt_wrap_sub (
  input  [31:0] x,
  input  [31:0] y,
  output [31:0] d
);
  assign d = x - y;
endmodule
