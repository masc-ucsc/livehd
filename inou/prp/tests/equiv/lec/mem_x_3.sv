/*
Negative control: `_1` with the two entry selects swapped.
:lec_expect: refuted
*/
module mem_x(input clk, rst, push_valid, addr, input [3:0] din, output [3:0] dout);
reg idle;
reg [3:0] \mem._mem[0] , \mem._mem[1] ;
always @(posedge clk) begin
  if (rst) idle <= 1; else idle <= !push_valid;
  if (push_valid && idle && addr) \mem._mem[0]  <= din;
  if (push_valid && idle && !addr) \mem._mem[1]  <= din;
end
assign dout = addr ? \mem._mem[1]  : \mem._mem[0] ;
endmodule
