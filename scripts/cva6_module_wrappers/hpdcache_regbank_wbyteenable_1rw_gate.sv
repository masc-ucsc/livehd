module hpdcache_regbank_wbyteenable_1rw_gate (
  input  logic        clk,
  input  logic        rst_n,
  input  logic        cs,
  input  logic        we,
  input  logic [3:0]  addr,
  input  logic [63:0] wdata,
  input  logic [7:0]  wbyteenable,
  output logic [63:0] rdata
);
  hpdcache_regbank_wbyteenable_1rw #(
    .ADDR_SIZE(4),
    .DATA_SIZE(64),
    .DEPTH(16)
  ) dut (
    .clk(clk),
    .rst_n(rst_n),
    .cs(cs),
    .we(we),
    .addr(addr),
    .wdata(wdata),
    .wbyteenable(wbyteenable),
    .rdata(rdata)
  );
endmodule
