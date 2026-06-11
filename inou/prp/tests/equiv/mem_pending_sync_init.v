module \mem_pending_sync_init.syncinit (
  input            clock,
  input      [1:0] raddr,
  output reg [3:0] q
);

  reg [3:0] data[3:0];
  reg [3:0] d0_mem;

  initial begin
    data[0] = 4'd1;
    data[1] = 4'd2;
    data[2] = 4'd3;
    data[3] = 4'd4;
  end

  // Match the current cgen_memory_1rd_1wr LATENCY_0=1 wrapper structure.
  always @(posedge clock) begin
    d0_mem <= data[raddr];
    q      <= d0_mem;
  end

endmodule
