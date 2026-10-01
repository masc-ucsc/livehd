// The memory is named `res`, the Pyrope binding of the __memory result, so prp-statematch pairs it BY NAME.
// Golden for spec_memory_rtl_named: 16x4 RF, two read ports whose (committed)
// read data is flopped once, one write port.
module spec_memory_rtl_named(input clk, input [3:0] raddr0, input [3:0] raddr1, input [3:0] wraddr,
                             input [3:0] din0, input we0, output reg [3:0] q0, output reg [3:0] q1);
  reg [3:0] res [0:15];
  always @(posedge clk) begin
    q0 <= res[raddr0];
    q1 <= res[raddr1];
    if (we0) res[wraddr] <= din0;
  end
endmodule
