module trivia_if(
  input  [2:0] a,
  input  [6:0] b,
  input        cond,
  output reg [3:0] res
);

  always @(*) begin
    if (cond)
      res = a + 4'd1;
    else
      res = b[3:0];
  end

endmodule
