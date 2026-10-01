// Golden: each local keeps its low 4 bits (wrap) or clamps at 15 (sat); the
// u8 outputs zero-extend the 4-bit value.
module wrap_typed_local(input [3:0] x, input [3:0] z,
                        output [7:0] y, output [7:0] y3, output [7:0] ys, output [7:0] c);
  wire [4:0] sum = x + z;
  wire [3:0] t3  = x + 4'd3;
  wire [3:0] low = sum[3:0];
  assign y  = {4'b0, x};
  assign y3 = {4'b0, t3};
  assign ys = sum > 5'd15 ? 8'd15 : {3'b0, sum};
  assign c  = {4'b0, low};
endmodule
