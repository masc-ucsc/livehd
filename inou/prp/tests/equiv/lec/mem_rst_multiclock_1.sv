/*
:lec_expect: refuted
MUTATED: the memory (write and sync clear) moved from clk_a to clk_b. Must REFUTE.
*/
module mem_rst_multiclock(input logic clk_a, input logic clk_b, input logic rst, input logic we,
                          input logic [1:0] wa, input logic [3:0] wd, input logic [1:0] ra,
                          input logic d, output logic [3:0] o, output logic z);
  logic [3:0] mem [4];
  always_ff @(posedge clk_b) begin
    if (rst) begin for (int i = 0; i < 4; i++) mem[i] <= '0; end
    else if (we) mem[wa] <= wd;
  end
  always_ff @(posedge clk_b) z <= d;
  assign o = mem[ra];
endmodule
