// A memory written on wr_clk with its output registered on rd_clk (the
// br_ram_flops shape). The netlist must PROVE against the RTL: the memory's
// own clock is what makes the design two-clock. The RTL-vs-RTL soundness
// group is ../equiv/lec/mem_wrrd_clk*.sv.
module mem_wrrd_clk(input logic wr_clk, input logic rd_clk, input logic we, input logic [1:0] wa,
                    input logic [3:0] wd, input logic [1:0] ra, output logic [3:0] q);
  logic [3:0] mem [4];
  always_ff @(posedge wr_clk) if (we) mem[wa] <= wd;
  always_ff @(posedge rd_clk) q <= mem[ra];
endmodule
