
module shiftx(input [7:0] a, input [7:0] b, output [7:0] c, output [7:0] d);

//assign c = $signed(b) < 0 ? a << -b : a >> b;
  assign c = a << $signed(-b);
  assign d = a;
//assign d = a[$signed(b) +: 8];

//wire [3:0] e = a[-2 +: 4];

endmodule

