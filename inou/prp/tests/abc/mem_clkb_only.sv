// The memory is the ONLY state on clk_b (combinational read), and the one
// flop is on clk_a. The mapped netlist blasts the memory into clk_b flops;
// pass/lec's multi-clock mode must commit the reference Memory on clk_b's
// edge, not every step (it was falsely REFUTED).
module mem_clkb_only(input logic clk_a, input logic clk_b, input logic we, input logic [1:0] wa,
                     input logic [3:0] wd, input logic [1:0] ra, input logic d,
                     output logic [3:0] o, output logic z);
  logic [3:0] mem [4];
  always_ff @(posedge clk_b) if (we) mem[wa] <= wd;
  always_ff @(posedge clk_a) z <= d;
  assign o = mem[ra];
endmodule
