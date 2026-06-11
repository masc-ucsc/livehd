module \hotmux_unique.dec (
  input  [1:0] x,
  input  [7:0] a,
  input  [7:0] b,
  input  [7:0] c,
  input  [7:0] d,
  output reg [7:0] res
);

  always @(*) begin
    if (x == 2'd0)
      res = a;
    else if (x == 2'd1)
      res = b;
    else if (x == 2'd2)
      res = c;
    else
      res = d;
  end

endmodule
