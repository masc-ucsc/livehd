// 1a-mem follow-up: initial-block memory contents ($mem_v2 INIT) + a sync
// read port (RD_CLK_ENABLE -> LATENCY_0=1). Pins the lgyosys_tolg INIT
// import and the RD_DATA partial-chunk segment registration (yosys narrows
// the 4-bit array to 3 bits, so RD_DATA lands on a sub-slice of the dout
// wire).
module mem_sync_init
( input            clock,
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

  always @(posedge clock) begin
    q <= data[raddr];
  end

endmodule
