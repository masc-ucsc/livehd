module hier_sub(input [7:0] x, input [7:0] y, output [7:0] s, output [7:0] t);
  assign s = x + y; assign t = x ^ y;
endmodule
module hier(input [7:0] a, input [7:0] b, output [7:0] o, output [7:0] p);
  wire [7:0] u, v;
  hier_sub i0(.x(a), .y(b), .s(u), .t(v));
  assign o = u & v; assign p = u | b;
endmodule
