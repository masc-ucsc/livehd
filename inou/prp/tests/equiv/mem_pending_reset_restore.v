module \mem_pending_reset_restore.restore (
  input            clock,
  input            reset,
  input      [3:0] a,
  input            i,
  input            we,
  output     [3:0] z
);

  reg [3:0] t[1:0];

  initial begin
    t[0] = 4'd4;
    t[1] = 4'd9;
  end

  // A memory has no parallel reset port, so `= (4, 9)` is restored by a SWEEP:
  // one entry per cycle while reset is held, driven by a counter that parks at
  // 0 whenever reset is low (so every reset pulse sweeps from entry 0). A full
  // restore therefore takes SIZE cycles of reset, not one.
  reg t_rstcnt;

  always @(posedge clock) begin
    if (!reset) t_rstcnt <= 1'b0;
    else        t_rstcnt <= (t_rstcnt == 1'b1) ? t_rstcnt : t_rstcnt + 1'b1;

    if (reset)   t[t_rstcnt] <= t_rstcnt ? 4'd9 : 4'd4;
    else if (we) t[i] <= a;
  end

  // `z = t[i]` sits BEFORE `t[i] = a` in the Pyrope source and memories default
  // to ordering="program", so the read observes the COMMITTED contents — which
  // is exactly what this fixture's header has always claimed. Until `ordering`
  // existed the lowering forwarded position-blind and this line read
  // `(!reset && we) ? a : t[i]`.
  assign z = t[i];

endmodule
