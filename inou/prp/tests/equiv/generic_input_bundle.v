module top(input [7:0] a, input \ctl.valid , input [7:0] \ctl.data , output [7:0] y);
  assign y = \ctl.valid  ? \ctl.data  : a;
endmodule
