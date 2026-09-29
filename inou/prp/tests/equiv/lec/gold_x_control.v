/*
*/
module top(input sel, output q);
  assign q = (sel ? 1'b0 : 1'bx) & 1'b0;
endmodule
