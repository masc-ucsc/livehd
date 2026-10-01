// Golden for mem_two_same_shape: two same-shape arrays written from a for-loop.
module mem_two_same_shape(input clk, input we, input [1:0] wa, input [3:0] wd, input [1:0] ra,
                          output [3:0] q);
  reg [3:0] ma [0:3];
  reg [3:0] mb [0:3];
  integer i;
  always @(posedge clk)
    for (i = 0; i < 4; i = i + 1)
      if (we && wa == i) begin
        ma[i] <= wd;
        mb[i] <= wd ^ 4'hf;
      end
  assign q = ma[ra] ^ {mb[ra][1:0], 2'b00};
endmodule
