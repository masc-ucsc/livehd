module Sum(
  input  [1:0] a,
  input  [1:0] b,
  output [2:0] o1,
  output [2:0] o2
);
  wire [2:0] _GEN_0 = {{1'd0}, a};
  wire [2:0] _GEN_1 = b + 2'h1;
  assign o1 = a + b;
  assign o2 = _GEN_0 + _GEN_1;
endmodule
module FinalVal2Test(
  input  [1:0] a,
  input  [1:0] b,
  output [3:0] out
);
  wire [1:0] submod_a;
  wire [1:0] submod_b;
  wire [2:0] submod_o1;
  wire [2:0] submod_o2;
  wire [2:0] _T = submod_o1;
  wire [2:0] _T_1 = submod_o2;
  Sum submod (
    .a(submod_a),
    .b(submod_b),
    .o1(submod_o1),
    .o2(submod_o2)
  );
  assign out = _T + _T_1;
  assign submod_a = a;
  assign submod_b = a;
endmodule
