/*
:lec_top: mem_multiclock
A memory written and read (registered) on clk_a, plus one unrelated flop on
clk_b that makes the design multi-clock. In multi-clock mode every state
element commits on a detected edge of its OWN clock; a Memory used to commit
every step whatever its clock did (pass/lec encode), so moving its write clock
was PROVEN (variant _1) and its own mapped netlist was REFUTED
(../../abc/mem_multiclock.sv).
*/
module mem_multiclock(input logic clk_a, input logic clk_b, input logic we, input logic [1:0] wa,
                      input logic [3:0] wd, input logic [1:0] ra, input logic d,
                      output logic [3:0] q, output logic z);
  logic [3:0] mem [4];
  always_ff @(posedge clk_a) if (we) mem[wa] <= wd;
  always_ff @(posedge clk_a) q <= mem[ra];
  always_ff @(posedge clk_b) z <= d;
endmodule
