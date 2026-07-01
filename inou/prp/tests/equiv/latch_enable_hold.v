// Golden for latch_enable_hold: level-sensitive latch.
module \latch_enable_hold.latch8 (
  input        en,
  input  [7:0] d,
  output reg [7:0] q
);
  always @(*) begin
    if (en)
      q = d;
  end
endmodule
