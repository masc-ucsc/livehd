
module simple_flop(input c, input d, output reg q);

always @ (posedge c)
  q <= d;

endmodule

