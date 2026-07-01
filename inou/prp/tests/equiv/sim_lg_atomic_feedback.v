// Golden for sim_lg_atomic_feedback: false combinational loop through an
// atomic sub-instance. Declarative Verilog and LEC handle this, but lhd
// `--emit-dir sim:` currently rejects it as `comb-loop-through-instance`.
module sim_lg_atomic_feedback_subadd(
  input  [7:0] a,
  input  [7:0] b,
  input  [7:0] c,
  input  [7:0] d,
  output [8:0] x,
  output [8:0] y
);
  assign x = a + b;
  assign y = c + d;
endmodule

module sim_lg_atomic_feedback_top(
  input  [7:0] a,
  input  [7:0] b,
  input  [7:0] d,
  input  [7:0] topx,
  output [8:0] x_out,
  output [8:0] y_out
);
  wire [8:0] x;
  wire [7:0] c;
  sim_lg_atomic_feedback_subadd u(.a(a), .b(b), .c(c), .d(d), .x(x), .y(y_out));
  assign c = x[7:0] + topx;
  assign x_out = x;
endmodule
