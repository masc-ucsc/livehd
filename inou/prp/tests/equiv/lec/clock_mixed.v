/*
:lec_top: dut
This pair exists to prove the ENGINE handles a design mixing posedge and negedge
flops. Without disabling the structural shortcut it is answered by semdiff
("MATCHED (semdiff structural, no solver)", 0 solver calls), so it exercises the
clock encoder not at all. Same reason as latch_polarity.v / pack_local.prp.
:lec_set: formal.lec.semdiff=none
*/
module dut(input clk, input [7:0] d, output reg [7:0] q);
  reg [7:0] a;
  always @(posedge clk) a <= d + 8'd3;
  always @(negedge clk) q <= a;
endmodule
