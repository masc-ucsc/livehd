// Golden for latch_hold_basic: a plain transparent-high D latch.
// Q follows D while `en` is asserted and holds when it deasserts.
module \latch_hold_basic.lhold8 (
  input            en,
  input      [7:0] d,
  output reg [7:0] q
);
  always_latch begin
    if (en)
      q <= d;
  end
endmodule
