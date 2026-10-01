// Golden for spec_memory_ordering_fwd: position-blind forwarding.
module spec_memory_ordering_fwd(input clk, input we, input [1:0] wa, input [3:0] wd,
                                input [1:0] ra, output [3:0] pre);
  reg [3:0] mem [0:3];
  always @(posedge clk) begin
    if (we) mem[wa] <= wd;
  end
  assign pre = (we && wa == ra) ? wd : mem[ra];
endmodule
