/*
:lec_top: mem_x
:lec_set: formal.engine=bmc formal.min_timeout=1 formal.simfail=false
Unknown power-on controls must not manufacture memory-vs-flop counterexamples.
The reference writes a real 2-entry memory when `push_valid && !valid`, and
`valid` is unknown until the reset prologue lands. `_1` splits the memory into
two flops and keeps the complement `idle` instead of `valid`, so a free choice
of the reference's power-on enable must not decide the stored word: the
reference memory carries a knowledge plane that merges the write and hold
paths under an unknown enable. `_2` corrupts the data and `_3` swaps the entry
select; both must still be refuted.
*/
module mem_x(input clk, rst, push_valid, addr, input [3:0] din, output [3:0] dout);
reg valid;
reg [3:0] mem [2];
always @(posedge clk) begin
  if (rst) valid <= 0; else valid <= push_valid;
  if (push_valid && !valid) mem[addr] <= din;
end
assign dout = mem[addr];
endmodule
