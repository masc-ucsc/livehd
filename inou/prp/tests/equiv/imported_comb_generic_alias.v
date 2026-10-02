module top(input [7:0] a, output [8:0] y);
  wire [7:0] t = ~a;
  assign y = ~{1'b0, t};
endmodule
