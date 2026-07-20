// Golden for latch_active_low: transparent-LOW D latch (gate active low).
module \latch_active_low.llow8 (
  input            clk,
  input      [7:0] d,
  output reg [7:0] q
);
  always_latch begin
    if (!clk)
      q <= d;
  end
endmodule
