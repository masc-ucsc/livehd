// array_selfref.v's Type-C self-reference written with a NET INITIALIZER
// (`wire [N-1:0][W-1:0] x = '{...};`) instead of a separate `assign`. That is
// how firtool spells every combinational lane table, and slang binds it via
// bindRValue WITHOUT wrapping it in an AssignmentExpression -- so a collector
// that only visits assignments never sees the driver and the array looks
// undriven.
//
// The two spellings must classify identically. When the net-initializer
// spelling missed the Type-C check the array was declared an ordinary `mut`,
// the sibling read `vec[0]` bound to the net's POISON init instead of to
// vec[0]'s driver, and `lhd lec` REFUTED with `z(ref=0 impl=1) @ a_0=1,a_1=1`
// (emitted Pyrope: `mut vec:u4 = 0 / vec = 0sb? / z = (a_0 | ...)#[2..=3]`).
// Spelling the net a Pyrope `wire` hands the repair to graph/split_selfref's
// split_packed_selfref_wire, which dissolves the false word-level cycle.
module \array_selfref_netinit.top (
  input  [1:0] a_0,
  input  [1:0] a_1,
  output [1:0] z
);
  wire [1:0][1:0] vec = '{vec[0] ^ a_1, a_0};  // vec[1] reads sibling vec[0] in its own initializer
  assign z = vec[1];
endmodule
