module hpdcache_regbank_wmask_1rw_gate (
  input  logic        clk,
  input  logic        rst_n,
  input  logic        cs,
  input  logic        we,
  input  logic [3:0]  addr,
  input  logic [63:0] wdata,
  input  logic [63:0] wmask,
  output logic [63:0] rdata
);

  hpdcache_regbank_wmask_1rw #(
    .ADDR_SIZE(4),
    .DATA_SIZE(64),
    .DEPTH    (16)
  ) dut (
    .clk,
    .rst_n,
    .cs,
    .we,
    .addr,
    .wdata,
    .wmask,
    .rdata
  );

endmodule
