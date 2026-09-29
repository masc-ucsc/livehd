/*
:lec_top: mem_x
:lec_set: formal.engine=bmc formal.min_timeout=1 formal.simfail=false
mem_x with the sides swapped: the flop pair is now the reference, so each
flop's write enable reads the power-on-unknown `idle`. The reference flop's
knowledge plane must merge its write and hold paths under that unknown
enable, not let the solver pick one and refute the memory implementation.
*/
module mem_x(input clk, rst, push_valid, addr, input [3:0] din, output [3:0] dout);
reg idle;
reg [3:0] \mem._mem[0] , \mem._mem[1] ;
always @(posedge clk) begin
  if (rst) idle <= 1; else idle <= !push_valid;
  if (push_valid && idle && !addr) \mem._mem[0]  <= din;
  if (push_valid && idle && addr) \mem._mem[1]  <= din;
end
assign dout = addr ? \mem._mem[1]  : \mem._mem[0] ;
endmodule
