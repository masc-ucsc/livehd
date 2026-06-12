module \condout_seed_uncertain.seedmod (
  input s,
  input [2:0] t,
  output reg [4:0] b
);

  always @(*) begin
    if (s) b = {2'b00, t};
    else   b = 5'd0;
  end

endmodule
