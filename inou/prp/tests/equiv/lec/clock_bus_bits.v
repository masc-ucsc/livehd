/*
:lec_top: cbb
Clocks that are BITS OF ONE BUS (`input [1:0] clks`, `posedge clks[i]`). The
LEC clock resolver peeled every Get_mask down to the whole input regardless of
the selected bit, so clks[0] and clks[1] were ONE clock `clks`: the design was
encoded single-clock and moving the memory write (variant _1) or a flop
(variant _2) from clks[0] to clks[1] was PROVEN. The same designs with two
separately named clock inputs were correctly REFUTED. Variant _3 respells the
bit selects through a wire and must PROVE.
*/
module cbb(input [1:0] clks, input we, input [1:0] wa, input [1:0] ra, input [3:0] wd, input d,
           output reg [3:0] q, output reg s);
  reg [3:0] mem [0:3];
  always @(posedge clks[0]) if (we) mem[wa] <= wd;
  always @(posedge clks[1]) q <= mem[ra];
  always @(posedge clks[0]) s <= d;
endmodule
