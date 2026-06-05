module \pipe3_mul.mul (
  input             clock,
  input      [15:0] a,
  input      [15:0] b,
  output reg [31:0] c
);

  reg [31:0] p0, p1;

  always @(posedge clock) begin
    p0 <= a * b;
    p1 <= p0;
    c  <= p1;
  end

endmodule
