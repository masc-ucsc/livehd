// Golden for sim_loop_mux_arm: the packed-word-through-a-Mux feedback is a
// false loop, so the module is a plain acyclic function of its inputs:
//   low = sel ? a : a^9;  hi = low ^ b;  z = {hi, low}
module sim_loop_mux_arm(
  input        sel,
  input  [3:0] a,
  input  [3:0] b,
  output [7:0] z
);
  wire [3:0] low = sel ? a : (a ^ 4'd9);
  wire [3:0] hi  = low ^ b;
  assign z = {hi, low};
endmodule
