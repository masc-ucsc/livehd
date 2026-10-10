module fz(input clock, input reset, input [3:0] a, output [3:0] o0);
  reg [3:0] lh;   // transparent while the clock is HIGH: opens at the rising edge
  always @(*) if (clock) lh = a;
  reg [3:0] q;    // samples lh at that same rising edge
  always @(posedge clock) if (reset) q <= 0; else q <= lh;
  assign o0 = q;
endmodule
