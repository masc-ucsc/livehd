module sum (
  input  [1:0] a,
  input  [1:0] b,
  output [2:0] o1,
  output [2:0] o2
);

assign o1 = a + b;
assign o2 = a + b + 1;

endmodule
