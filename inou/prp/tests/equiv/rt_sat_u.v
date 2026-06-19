// Golden for runtime unsigned `sat`: clamp to [0, 15].
module \rt_sat_u.rt_sat_u (
  input  [7:0] a,
  output [3:0] z
);
  assign z = (a > 8'd15) ? 4'hf : a[3:0];
endmodule
