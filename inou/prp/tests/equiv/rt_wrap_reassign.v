// Golden for runtime `wrap` of a subtraction into a re-assigned u32 local: the
// dead prior `d = x` is overwritten by the wrapped difference, which keeps the
// low 32 bits of `x - y` — Verilog's modular subtraction.
module \rt_wrap_reassign.rt_wrap_reassign (
  input  [31:0] x,
  input  [31:0] y,
  output [31:0] o
);
  assign o = x - y;
endmodule
