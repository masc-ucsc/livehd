// This is a test case for asynchronous load DFFs.
module aldff(input clk, input aload, input [7:0] din_load, input [7:0] din, output [7:0] q);
  always @(negedge clk, posedge aload) begin 
    if (aload)
      q <= din_load;
    else
      q <= din;
  end
endmodule
