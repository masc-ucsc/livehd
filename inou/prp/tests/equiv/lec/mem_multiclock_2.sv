/*
Equivalent rewrite (positive control): same clocks, write and read spelled
in one process with an inverted enable test, and the registered read
double-inverted. The process/enable rewrite alone canonicalizes back to the
same graph (semdiff matched it with no solver call); the `~(~...)` forces the
encoder's multi-clock memory edge gating to decide it. Must PROVE.
:lec_grep_not: structurally identical
*/
module mem_multiclock(input logic clk_a, input logic clk_b, input logic we, input logic [1:0] wa,
                      input logic [3:0] wd, input logic [1:0] ra, input logic d,
                      output logic [3:0] q, output logic z);
  logic [3:0] mem [4];
  always_ff @(posedge clk_a) begin
    q <= ~(~mem[ra]);
    if (!we) begin end else mem[wa] <= wd;
  end
  always_ff @(posedge clk_b) z <= d;
endmodule
