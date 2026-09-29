module array_reference(input clk_i, en_i, input [2:0] wa_i, wd_i, ra_i, output [2:0] rd_o);
  reg [2:0] q[8];
  always @(posedge clk_i) if (en_i) q[wa_i] <= wd_i;
  assign rd_o = q[ra_i];
endmodule
