module selfref_rows (
  input  [5:0] a,
  output [17:0] y
);

  // Row r is the lowest set bit of what the earlier rows left.
  wire [5:0] r0 = a & (~a + 1'b1);
  wire [5:0] m1 = a & ~r0;
  wire [5:0] r1 = m1 & (~m1 + 1'b1);
  wire [5:0] m2 = m1 & ~r1;
  wire [5:0] r2 = m2 & (~m2 + 1'b1);

  assign y = {r2, r1, r0};

endmodule
