module \mem_init_scalar.regi (
  input            clock,
  input            reset,
  input      [7:0] a,
  input      [1:0] i,
  input            we,
  output     [7:0] z
);

  reg [7:0] t[3:0];

  // The initializer is BOTH the power-on contents and the reset value, so the
  // module carries a reset even though the source never names one.
  initial begin
    t[0] = 8'd3;
    t[1] = 8'd3;
    t[2] = 8'd3;
    t[3] = 8'd3;
  end

  // A memory has no parallel reset port: the reset value is restored one entry
  // per cycle by a sweep counter, so a full restore takes SIZE cycles of reset
  // held high. The counter parks at 0 while reset is low, so every reset pulse
  // sweeps from entry 0.
  reg [1:0] t_rstcnt;

  always @(posedge clock) begin
    if (!reset) t_rstcnt <= 2'd0;
    else        t_rstcnt <= (t_rstcnt == 2'd3) ? t_rstcnt : t_rstcnt + 2'd1;

    if (reset)   t[t_rstcnt] <= 8'd3;  // restore sweep
    else if (we) t[i] <= a;          // program writes are suppressed in reset
  end

  // same index for read and write + fwd (the restore port never forwards)
  assign z = (we && !reset) ? a : t[i];

endmodule
