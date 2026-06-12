module \condout_const_arms.condout (
  input s,
  output reg [1:0] o
);

  always @(*) begin
    if (s) o = 2'd1;
    else   o = 2'd2;
  end

endmodule
