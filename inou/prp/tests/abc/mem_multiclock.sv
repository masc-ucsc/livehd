// A memory on clk_a (write + registered read) and one unrelated flop on
// clk_b. Its mapped netlist blasts the memory into flops on clk_a, which commit
// on clk_a's detected edge; the reference Memory used to commit EVERY step in
// pass/lec's multi-clock mode, so this netlist was falsely REFUTED.
// The RTL-vs-RTL soundness group is ../equiv/lec/mem_multiclock*.sv.
module mem_multiclock(input logic clk_a, input logic clk_b, input logic we, input logic [1:0] wa,
                      input logic [3:0] wd, input logic [1:0] ra, input logic d,
                      output logic [3:0] q, output logic z);
  logic [3:0] mem [4];
  always_ff @(posedge clk_a) if (we) mem[wa] <= wd;
  always_ff @(posedge clk_a) q <= mem[ra];
  always_ff @(posedge clk_b) z <= d;
endmodule
