// Golden for mod_general: native Verilog `%`, which is TRUNCATED (the result
// takes the sign of the dividend) -- the semantics Ntype_op::Rem is defined to
// have. `rs` is signed on both sides so a floored-modulo encoding would differ.
module \mod_general.foo (
  input  [7:0]        a,
  input  [7:0]        b,
  input  signed [7:0] s,
  output [7:0]        rr,
  output [2:0]        r5,
  output signed [7:0] rs
);
  assign rr = a % b;
  assign r5 = a % 5;
  assign rs = s % 8;
endmodule
