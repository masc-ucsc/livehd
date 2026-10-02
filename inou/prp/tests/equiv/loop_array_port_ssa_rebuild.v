// Golden for loop_array_port_ssa_rebuild: the sum of the four byte lanes of
// the packed array port (lane 0 = bits [7:0]).
module \loop_array_port_ssa_rebuild.top (input [31:0] b, output [11:0] z);
  assign z = {4'b0, b[7:0]} + {4'b0, b[15:8]} + {4'b0, b[23:16]} + {4'b0, b[31:24]};
endmodule
