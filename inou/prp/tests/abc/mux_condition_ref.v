module reference(input [7:0] a, output [3:0] r);
  wire [2:0] count = a[7] ? 3'd0 : a[6] ? 3'd1 : a[5] ? 3'd2 : 3'd7;
  assign r = {a == 0, ~count};
endmodule
