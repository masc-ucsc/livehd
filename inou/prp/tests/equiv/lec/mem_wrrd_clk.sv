/*
:lec_top: mem_wrrd_clk
A memory written on wr_clk whose output is registered on rd_clk: the memory's
clock is the only thing that makes the design two-clock. Variant _1 moves the
write to rd_clk and must REFUTE (it was PROVEN: neither side's census counted a
memory clock, so both encoded as single-clock); variant _2 is an equivalent
rewrite and must PROVE.
*/
module mem_wrrd_clk(input logic wr_clk, input logic rd_clk, input logic we, input logic [1:0] wa,
                    input logic [3:0] wd, input logic [1:0] ra, output logic [3:0] q);
  logic [3:0] mem [4];
  always_ff @(posedge wr_clk) if (we) mem[wa] <= wd;
  always_ff @(posedge rd_clk) q <= mem[ra];
endmodule
