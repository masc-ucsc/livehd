// Golden for spec_memory_default_program: program order -- the read before the
// write sees the committed word, the read after it sees the write.
module spec_memory_default_program(input clk, input we, input [1:0] wa, input [3:0] wd,
                                   input [1:0] ra, output [3:0] pre, output [3:0] post);
  reg [3:0] mem [0:3];
  always @(posedge clk) begin
    if (we) mem[wa] <= wd;
  end
  assign pre  = mem[ra];
  assign post = (we && wa == ra) ? wd : mem[ra];
endmodule
