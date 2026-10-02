/*
Equivalent rewrite (positive control): the read goes through a named,
double-inverted wire. A plain named wire canonicalizes away (semdiff matched it
with no solver call); the `~(~...)` forces the encoder to decide it.
:lec_grep_not: structurally identical
*/
module mem_wrrd_clk(input logic wr_clk, input logic rd_clk, input logic we, input logic [1:0] wa,
                    input logic [3:0] wd, input logic [1:0] ra, output logic [3:0] q);
  logic [3:0] mem [4];
  logic [3:0] rd;
  assign rd = ~(~mem[ra]);
  always_ff @(posedge wr_clk) begin
    if (we) mem[wa] <= wd;
  end
  always_ff @(posedge rd_clk) q <= rd;
endmodule
