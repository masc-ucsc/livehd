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

  // The reset value is restored to every entry in ONE cycle of reset, exactly
  // like a scalar reg; program writes are suppressed while reset is held.
  always @(posedge clock) begin
    if (reset) begin
      t[0] <= 8'd3; t[1] <= 8'd3; t[2] <= 8'd3; t[3] <= 8'd3;
    end else if (we) begin
      t[i] <= a;
    end
  end

  // same index for read and write + fwd (a write suppressed by reset is not
  // forwarded)
  assign z = (we && !reset) ? a : t[i];

endmodule
