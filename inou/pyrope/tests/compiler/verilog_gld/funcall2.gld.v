module sum2 (
  input  [1:0] a,
  input  [1:0] b,
  output [2:0] o
);

assign o = a + b;

endmodule

module funcall2(out);
  wire [1:0] lg_0;
  wire [2:0] lg_1;
  wire [2:0] lg_2;
  output [2:0] out;
  sum2 lg_subgraph17 (
    .a(lg_1),
    .b(lg_0),
    .o(lg_2),
  );

  assign lg_0 = 2'h3;
  assign lg_1 = 2'h2;
  assign out = lg_2;
endmodule
