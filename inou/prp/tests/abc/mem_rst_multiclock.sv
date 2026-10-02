// A memory with a synchronous clear-on-rst (whole-array update) written on
// clk_a, plus one unrelated flop on clk_b. Its mapped netlist blasts the
// memory into flops on clk_a; pass/lec's multi-clock mode must gate the
// reference Memory's write AND its sync reset on clk_a's edge (encode's
// `mem_edge`), or this netlist is falsely REFUTED.
// The RTL-vs-RTL soundness group is ../equiv/lec/mem_rst_multiclock*.sv.
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
