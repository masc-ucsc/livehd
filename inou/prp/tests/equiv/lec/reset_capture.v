/*
:lec_set: formal.engine=bmc formal.min_timeout=1 formal.simfail=false
A single-clock reset prologue. `busy` has a synchronous reset; `data` has
none and loads whenever `busy` is set, INCLUDING the first reset cycle, when
`busy` still holds its arbitrary power-on value. After reset `data` keeps
whatever that first cycle captured until the next `busy` load, so it depends
on the pre-reset value of a reset-bearing flop. A mapped netlist may store
that flop complemented under the same name (`_1`); the reference's pre-reset
value must be tracked unknown, not shared with the complemented impl, or the
prologue manufactures a counterexample the hardware cannot have.
*/
module top(input clk, input rst, input start, input [3:0] din, output [3:0] q, output qb);
reg busy;
reg [3:0] data;
always @(posedge clk) begin
  if (rst) busy <= 1'b0; else busy <= start;
  if (busy) data <= din;
end
assign q = data;
assign qb = busy;
endmodule
