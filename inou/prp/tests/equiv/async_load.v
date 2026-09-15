module async_load(
  input clk, input aload, input [7:0] din_load, input [7:0] din,
  output [7:0] q
);
  reg [7:0] state;
  assign q = state;
  always @(negedge clk or posedge aload)
    if (aload)
      state <= din_load ^ 8'h5a;
    else
      state <= din;
endmodule
