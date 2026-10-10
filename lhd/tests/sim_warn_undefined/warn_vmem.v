// Verilog out-of-range memory accesses: the translation keeps the dropped
// write and the X read (the low address bits), and the slang reader adds an
// lgundef marker per access that the simulator reports once.
module warn_vmem(input clock, input reset, input [7:0] a, input [2:0] wa, input [2:0] ra, output [7:0] m);
  reg [7:0] mem [0:3];
  integer i;
  always @(posedge clock) if (reset) begin for (i = 0; i < 4; i = i + 1) mem[i] <= 0; end else mem[wa] <= a;
  assign m = mem[ra];
endmodule
