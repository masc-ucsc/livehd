module unsigned_slice_zext(input [2:0] x, input signed [3:0] s, output [7:0] y, output [7:0] z, output [3:0] w);
  assign y = {5'b0, x};
  assign z = {5'b0, x} + 8'd1;
  assign w = s;
endmodule
