module top(input [3:0] a, input [3:0] b, input signed [3:0] c, input e,
           output signed [7:0] o, output y, output n, output signed [7:0] z,
           output signed [7:0] w, output k);
  assign o = {b, a};
  assign y = o < 0;
  assign n = $signed({b, a}) < -8'sd64;
  assign z = {e, 7'd0};
  wire signed [7:0] cx = c;
  assign w = {cx[7:1], e};
  assign k = $signed({4'hf, a}) < -8'sd8;
endmodule
