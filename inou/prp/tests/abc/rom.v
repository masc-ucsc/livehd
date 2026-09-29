/*
:synth_set: pass.abc.memory=true
*/
module rom(input clock, en, input [3:0] addr, output [7:0] comb, output reg [7:0] q);
  reg [7:0] table_data [0:15];
  initial begin
    for (integer k=0; k<16; k=k+1) table_data[k] = k*k + 3*k + 7;
  end
  assign comb = table_data[addr];
  always @(posedge clock) if (en) q <= table_data[addr];
endmodule
