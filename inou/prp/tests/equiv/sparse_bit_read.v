module sparse_bit_read(input [7:0] x, output [3:0] even, output [3:0] odd);
  assign even = {x[6], x[4], x[2], x[0]};
  assign odd = {x[7], x[5], x[3], x[1]};
endmodule
