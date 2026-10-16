// Golden for out_bitassign_loop_in_if.prp.
module out_bitassign_loop_in_if(input [3:0] a, input s, output [3:0] o, output [3:0] p, output [3:0] q);
  assign o = ~a;
  assign p = {a[0], a[1], a[2], a[3]};
  assign q = s ? a : ~a;
endmodule
