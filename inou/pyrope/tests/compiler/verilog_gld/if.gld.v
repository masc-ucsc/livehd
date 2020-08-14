module if_gld (
  input        inp, 
  output [2:0] out
);

assign out = inp == 1'd0 ? 3'd4 : 3'd5;

endmodule
