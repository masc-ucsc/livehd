module if2 (
  input        inp, 
  output [2:0] out
);

assign out = inp == 0 ? 3'd4 : 3'd3;

endmodule
