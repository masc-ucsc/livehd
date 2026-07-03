module sim_loop_sticky_ripple(input [1:0] a, output [3:0] z);
  // Flattened: s[0]=0, s[1]=a[0], t=s[1]=a[0], s[2]=t=a[0], s[3]=0.
  assign z = {1'b0, a[0], a[0], 1'b0};
endmodule
