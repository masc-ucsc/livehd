/*
The 2-entry memory form of the reference.
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
