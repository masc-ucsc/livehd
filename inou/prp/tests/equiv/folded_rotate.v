module folded_rotate(input [3:0] a, valid, output [3:0] out);
  assign out = valid[0] ? {a[2:0], a[3]} : a;
endmodule
