module top(input [7:0] x, input [7:0] y, input s, output [7:0] p, output [7:0] q, output [7:0] r, output [7:0] k);
  wire [7:0] da = s ? y : x;
  wire [7:0] db = s ? (x ^ 8'd3) : y;
  assign p = da + db;
  wire [7:0] ea = s ? x : y;
  wire [7:0] eb = s ? y : 8'd5;
  assign q = ea + eb;
  wire [7:0] fx = s ? y : x;
  assign r = (fx ^ 8'd1) - fx;
  assign k = da ^ db;
endmodule
