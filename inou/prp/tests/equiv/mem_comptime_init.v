module \mem_comptime_init.mci (
  input            clock,
  input            reset,
  input            i,
  input            j,
  output     [3:0] z
);

  reg [3:0] t [0:3];

  initial begin
    t[0] = 4'd1; t[1] = 4'd2; t[2] = 4'd3; t[3] = 4'd4;
  end

  // A memory has no parallel reset port: the reset value is restored one entry
  // per cycle by a sweep counter (the per-entry values are a ROM lookup on it),
  // so a full restore takes SIZE cycles of reset held high. The counter parks at
  // 0 while reset is low, so every reset pulse sweeps from entry 0.
  reg [1:0] t_rstcnt;
  wire [3:0] t_rstval = (t_rstcnt == 2'd0) ? 4'd1
                      : (t_rstcnt == 2'd1) ? 4'd2
                      : (t_rstcnt == 2'd2) ? 4'd3
                      : 4'd4;

  always @(posedge clock) begin
    if (!reset) t_rstcnt <= 2'd0;
    else        t_rstcnt <= (t_rstcnt == 2'd3) ? t_rstcnt : t_rstcnt + 2'd1;

    if (reset) t[t_rstcnt] <= t_rstval;  // restore sweep
  end

  // async read of the row-major flat address i*2 + j
  assign z = t[{i, j}];

endmodule
