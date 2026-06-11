module \hotmux_match_partial.msel2 (
  input  [1:0] x,
  input  [7:0] a,
  input  [7:0] b,
  output reg [7:0] lo,
  output reg [7:0] hi
);

  always @(*) begin
    lo = 8'd0;
    hi = 8'd0;
    if (x == 2'd0) begin
      lo = a;
      hi = b;
    end else if (x == 2'd1) begin
      lo = b;
      hi = a;
    end else begin
      lo = 8'hff;
    end
  end

endmodule
