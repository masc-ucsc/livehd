module \mem_init.mem_init (
  input            clock,
  input            reset,
  input      [1:0] raddr,
  output     [7:0] q
);

  reg [7:0] mem [0:3];

  initial begin
    mem[0] = 8'd1; mem[1] = 8'd2; mem[2] = 8'd3; mem[3] = 8'd4;
  end

  // A memory has no parallel reset port: the reset value is restored one entry
  // per cycle by a sweep counter (the per-entry values are a ROM lookup on it),
  // so a full restore takes SIZE cycles of reset held high. The counter parks at
  // 0 while reset is low, so every reset pulse sweeps from entry 0.
  reg [1:0] mem_rstcnt;
  wire [7:0] mem_rstval = (mem_rstcnt == 2'd0) ? 8'd1
                        : (mem_rstcnt == 2'd1) ? 8'd2
                        : (mem_rstcnt == 2'd2) ? 8'd3
                        : 8'd4;

  always @(posedge clock) begin
    if (!reset) mem_rstcnt <= 2'd0;
    else        mem_rstcnt <= (mem_rstcnt == 2'd3) ? mem_rstcnt : mem_rstcnt + 2'd1;

    if (reset) mem[mem_rstcnt] <= mem_rstval;  // restore sweep
  end

  // async read; restore ports are never forwarded (a read during reset
  // returns the committed contents)
  assign q = mem[raddr];

endmodule
