// :test: roundtrip
// :lec_timeout: 30
module writer_dead_ternary #(parameter H=0)(input [3:0] data, input sel, output y);
  assign y = H==0 ? data[sel] : data[H-1];
endmodule
