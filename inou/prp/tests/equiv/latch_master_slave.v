// Golden for latch_master_slave: an opposite-phase latch pair, i.e. a posedge
// flop built from two latches. The master is transparent while clk is low, the
// slave while clk is high, so they are never simultaneously transparent and the
// pair borrows no time.
module \latch_master_slave.ms8 (
  input            clk,
  input      [7:0] d,
  output     [7:0] q
);
  reg [7:0] m;
  reg [7:0] s;

  always_latch begin
    if (!clk)
      m <= d;
  end

  always_latch begin
    if (clk)
      s <= m;
  end

  assign q = s;
endmodule
