module \condout_mixed_arms.mixedarm (
  input s,
  input [1:0] a,
  output reg [1:0] o
);

  always @(*) begin
    if (s) o = 2'd1;
    else   o = a;
  end

endmodule
