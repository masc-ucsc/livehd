module \mem_pending_sync_init.syncinit (
  input            clock,
  input      [1:0] raddr,
  output reg [3:0] q
);

  reg [3:0] data[3:0];

  initial begin
    data[0] = 4'd1;
    data[1] = 4'd2;
    data[2] = 4'd3;
    data[3] = 4'd4;
  end

  // type=1 (sync read, LATENCY_0=1): exactly ONE edge between the read
  // address and the data out — q@[1] at the interface.
  always @(posedge clock) begin
    q <= data[raddr];
  end

endmodule
