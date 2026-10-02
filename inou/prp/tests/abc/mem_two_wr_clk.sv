// A memory with two write ports on DIFFERENT clocks (clk_a, clk_b) and a
// combinational read. No single-clock flop can hold an entry two clocks
// write, so yosys `memory_map` refuses to blast it; the memory must stay a
// native (multiclock) instance. It used to be dropped silently: the
// `_blasted` module came out with an empty body and `o` undriven (rc=0, no
// diagnostic). The RTL-vs-RTL soundness group is ../equiv/lec/mem_two_wr_clk*.sv.
module mem_two_wr_clk(input logic clk_a, input logic clk_b, input logic we0, input logic we1,
                      input logic [1:0] wa0, input logic [1:0] wa1, input logic [3:0] wd0,
                      input logic [3:0] wd1, input logic [1:0] ra, output logic [3:0] o);
  logic [3:0] mem [4];
  always_ff @(posedge clk_a) if (we0) mem[wa0] <= wd0;
  always_ff @(posedge clk_b) if (we1) mem[wa1] <= wd1;
  assign o = mem[ra];
endmodule
