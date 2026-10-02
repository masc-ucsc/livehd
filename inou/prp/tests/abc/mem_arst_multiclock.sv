// Same as mem_rst_multiclock.sv but the clear is an ASYNC reset
// (`posedge clk_a or posedge rst`). The mapped netlist's blasted flops on
// clk_a must match a reference Memory gated on clk_a's edge in pass/lec's
// multi-clock mode (it was falsely REFUTED).
module mem_arst_multiclock(input logic clk_a, input logic clk_b, input logic rst, input logic we,
                           input logic [1:0] wa, input logic [3:0] wd, input logic [1:0] ra,
                           input logic d, output logic [3:0] o, output logic z);
  logic [3:0] mem [4];
  always_ff @(posedge clk_a or posedge rst) begin
    if (rst) begin for (int i = 0; i < 4; i++) mem[i] <= '0; end
    else if (we) mem[wa] <= wd;
  end
  always_ff @(posedge clk_b) z <= d;
  assign o = mem[ra];
endmodule
