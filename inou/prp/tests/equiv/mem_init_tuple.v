module \mem_init_tuple.regi2 (
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
    t[0] = 8'd1;
    t[1] = 8'd2;
    t[2] = 8'd3;
    t[3] = 8'd4;
  end

  // A memory has no parallel reset port: the reset value is restored one entry
  // per cycle by a sweep counter (a per-entry init is a ROM lookup on that
  // counter), so a full restore takes SIZE cycles of reset held high.
  reg [1:0] t_rstcnt;
  wire [7:0] rstval = (t_rstcnt == 2'd0) ? 8'd1
                    : (t_rstcnt == 2'd1) ? 8'd2
                    : (t_rstcnt == 2'd2) ? 8'd3
                                       : 8'd4;

  always @(posedge clock) begin
    if (!reset) t_rstcnt <= 2'd0;
    else        t_rstcnt <= (t_rstcnt == 2'd3) ? t_rstcnt : t_rstcnt + 2'd1;

    if (reset)   t[t_rstcnt] <= rstval;  // restore sweep
    else if (we) t[i] <= a;            // program writes are suppressed in reset
  end

  assign z = (we && !reset) ? a : t[i];

endmodule
