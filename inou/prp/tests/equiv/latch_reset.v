// Golden for latch_reset (todo/livehd/2f-latch M7): a transparent-high D latch
// with an ASYNCHRONOUS reset to 3. The reset branch precedes the transparency
// test — a latch has no clock edge to synchronize a reset to, so it cannot be
// anything but async. This is the $adlatch shape.
module \latch_reset.lrst8 (
  input            en,
  input      [7:0] d,
  output reg [7:0] q,
  input            reset
);
  always_latch begin
    if (reset)
      q <= 8'd3;
    else if (en)
      q <= d;
  end
endmodule
