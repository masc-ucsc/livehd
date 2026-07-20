// Golden for latch_mixed_flop: a transparent-high latch feeding a posedge flop.
// Two state elements in two different commit classes.
module \latch_mixed_flop.mixed8 (
  input            clk,
  input            en,
  input      [7:0] d,
  output     [7:0] q
);
  reg [7:0] l;
  reg [7:0] f;

  always_latch begin
    if (en)
      l <= d;
  end

  always_ff @(posedge clk) begin
    f <= l;
  end

  assign q = f;
endmodule
