// Golden for spec_memory_ordering_none: a same-cycle read of a written address
// is undefined (x); everything else is the committed word.
module spec_memory_ordering_none(input clk, input we, input [1:0] wa, input [3:0] wd,
                                 input [1:0] ra, input [1:0] rb,
                                 output [3:0] pre, output [3:0] post);
  reg [3:0] mem [0:3];
  always @(posedge clk) begin
    if (we) mem[wa] <= wd;
  end
  assign pre  = (we && wa == ra) ? 4'bxxxx : mem[ra];
  assign post = (we && wa == rb) ? 4'bxxxx : mem[rb];
endmodule
