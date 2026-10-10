module top(input [63:0] a, input [4:0] rs, input rw, output y);
  assign y = rw && a[11:7] == rs;
endmodule
