/*
:lec_top: mem_rst_multiclock
A memory with a synchronous clear-on-rst (a whole-array update) written on
clk_a, plus one unrelated flop on clk_b that makes the design multi-clock.
The whole-array commit and the sync reset must be gated by the memory's own
clock edge (encode's `mem_edge`). Variant _1 moves the memory to clk_b and
must REFUTE (it was PROVEN); _2 is an equivalent rewrite and must PROVE. The
mapped-netlist side is ../../abc/mem_rst_multiclock.sv.
*/
module mem_rst_multiclock(input logic clk_a, input logic clk_b, input logic rst, input logic we,
                          input logic [1:0] wa, input logic [3:0] wd, input logic [1:0] ra,
                          input logic d, output logic [3:0] o, output logic z);
  logic [3:0] mem [4];
  always_ff @(posedge clk_a) begin
    if (rst) begin for (int i = 0; i < 4; i++) mem[i] <= '0; end
    else if (we) mem[wa] <= wd;
  end
  always_ff @(posedge clk_b) z <= d;
  assign o = mem[ra];
endmodule
