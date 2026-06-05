module \pipe2_two_out.two (
  input            clock,
  input      [3:0] a,
  input      [3:0] b,
  output reg [7:0] x,
  output reg [7:0] y
);

  reg [7:0] x0, y0;

  always @(posedge clock) begin
    x0 <= a + b;
    x  <= x0;
    y0 <= a & b;
    y  <= y0;
  end

endmodule
