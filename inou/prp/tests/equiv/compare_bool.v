module compare_bool(
  input  [7:0] a,
  input  [7:0] b,
  output       eq,
  output       ne,
  output       lt,
  output       le,
  output       gt,
  output       ge
);
assign eq = a == b;
assign ne = a != b;
assign lt = a < b;
assign le = a <= b;
assign gt = a > b;
assign ge = a >= b;
endmodule
