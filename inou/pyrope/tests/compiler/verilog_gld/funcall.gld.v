module sum (
  input  [1:0] a,
  input  [1:0] b,
  output [2:0] o1,
  output [2:0] o2
);

assign o1 = a + b;
assign o2 = a + b + 1;

endmodule

module funcall(out2, out);
  wire [1:0] lg_0;
  wire [2:0] lg_1;
  wire [2:0] lg_2;
  output [2:0] out;
  output [2:0] out2;
  sum lg_subgraph17 (
    .a(lg_0),
    .b(lg_0),
    .o1(lg_1),
    .o2(lg_2)
  );
  assign lg_0 = 2'h3;
  assign out2 = lg_2;
  assign out = lg_1;
endmodule
