module \wire_ring.ring (input clock, input reset, input [7:0] a, output [7:0] out);
  reg [7:0] acc;
  assign out = acc;        // fb is just a net == acc; no flop for fb
  always @(posedge clock) acc <= reset ? 8'h0 : (a + acc);
endmodule
