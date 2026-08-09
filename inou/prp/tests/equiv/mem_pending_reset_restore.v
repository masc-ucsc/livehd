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

  always @(posedge clock) begin
    if (reset) begin
      t[0] <= 4'd4;
      t[1] <= 4'd9;
    end else if (we) begin
      t[i] <= a;
    end
  end

  // `z = t[i]` sits BEFORE `t[i] = a` in the Pyrope source and memories default
  // to ordering="program", so the read observes the COMMITTED contents — which
  // is exactly what this fixture's header has always claimed. Until `ordering`
  // existed the lowering forwarded position-blind and this line read
  // `(!reset && we) ? a : t[i]`.
  assign z = t[i];

endmodule
