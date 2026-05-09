module wrap_sat(
  input        [7:0] x,
  input signed [7:0] y,
  output       [3:0] wrapped,
  output       [3:0] sat_u,
  output signed [3:0] sat_s
);
assign wrapped = x[3:0];
assign sat_u   = (x > 8'd15) ? 4'd15 : x[3:0];
assign sat_s   = (y > 8'sd7)  ? 4'sd7  :
                 (y < -8'sd8) ? -4'sd8 :
                                  y[3:0];
endmodule
