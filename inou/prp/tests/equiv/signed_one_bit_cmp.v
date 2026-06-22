module p(input [7:0] x, output b);
  assign b = x[3];   // signed(x#[3]) < 0  ==  bit 3 set
endmodule
