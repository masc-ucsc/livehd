// Golden for spec_memory_ordering_old: reads always see the committed contents.
module spec_memory_ordering_old(input clk, input we, input [1:0] wa, input [3:0] wd,
                                input [1:0] ra, output [3:0] post);
  reg [3:0] mem [0:3];
  always @(posedge clk) begin
    if (we) mem[wa] <= wd;
  end
  assign post = mem[ra];
endmodule
