/*
:lec_top: split
:lec_set: formal.engine=bmc formal.bound=2 formal.lec.hier=false
*/
module split(input clk, input rst, input [3:0] in, output [3:0] out);
  reg [3:0] stage;
  always @(posedge clk) begin
    if (rst) stage <= 4'b0;
    else stage <= in;
  end
  assign out = stage;
endmodule
