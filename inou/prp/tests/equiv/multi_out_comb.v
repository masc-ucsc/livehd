module \multi_out_comb.top (
  input [7:0] x,
  output [8:0] os,
  output [8:0] od
);
  assign os = x + 9'd1;
  assign od = x + 9'd2;
endmodule
