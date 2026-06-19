// Golden for runtime signed `sat`: clamp to [-8, 7].
module \rt_sat_s.rt_sat_s (
  input  signed [7:0] a,
  output signed [3:0] z
);
  assign z = ($signed(a) > 8'sd7)  ? 4'sd7  :
             ($signed(a) < -8'sd8) ? -4'sd8 : a[3:0];
endmodule
