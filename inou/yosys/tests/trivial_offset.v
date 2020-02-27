module trivial_offset(a, y);
  input [7:0] a;
  output [3:0] y;

  assign y = a[7:4] & a[3:0];

endmodule
