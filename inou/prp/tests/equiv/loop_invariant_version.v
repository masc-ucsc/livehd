module \loop_invariant_version.top (input [7:0] a, output [11:0] z);
  assign z = {4'b0, a} * 12'd6;
endmodule
