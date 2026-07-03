// Golden for sim_sub_nested_comb_feedback: false combinational loop through a
// pure-comb sub-instance that itself contains a NESTED comb sub (ExeUnitImp_4/
// Alu/AluDataModule). The mid instance's output cones are independent, so the
// feedback x -> c is bit-level acyclic; declarative Verilog and LEC are fine,
// but `--emit-dir sim:` schedules the instance atomically and the pure-comb
// flatten declines a callee that contains a Sub.
module ssncf_leaf_add(
  input  [7:0] a,
  input  [7:0] b,
  output [8:0] s
);
  assign s = a + b;
endmodule

module ssncf_mid(
  input  [7:0] a,
  input  [7:0] b,
  input  [7:0] c,
  input  [7:0] d,
  output [8:0] x,
  output [8:0] y
);
  ssncf_leaf_add u_leaf(.a(a), .b(b), .s(x));
  assign y = c + d;
endmodule

module sim_sub_nested_comb_feedback_top(
  input  [7:0] a,
  input  [7:0] b,
  input  [7:0] d,
  input  [7:0] topx,
  output [8:0] x_out,
  output [8:0] y_out
);
  wire [8:0] x;
  wire [7:0] c;
  ssncf_mid u(.a(a), .b(b), .c(c), .d(d), .x(x), .y(y_out));
  assign c = x[7:0] + topx;
  assign x_out = x;
endmodule
