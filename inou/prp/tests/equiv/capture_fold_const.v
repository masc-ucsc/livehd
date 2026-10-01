module capture_fold_const(input [7:0] a, output [7:0] o1, output [7:0] o2, output [7:0] o3, output [7:0] o4);
  assign o1 = a + 8'd4;
  assign o2 = a + 8'd7;
  assign o3 = a + 8'd6;
  assign o4 = a + 8'd10;
endmodule
