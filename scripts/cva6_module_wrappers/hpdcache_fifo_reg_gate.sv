module hpdcache_fifo_reg_gate (
  input  logic        clk_i,
  input  logic        rst_ni,
  input  logic        w_i,
  output logic        wok_o,
  input  logic [15:0] wdata_i,
  input  logic        r_i,
  output logic        rok_o,
  output logic [15:0] rdata_o
);

  hpdcache_fifo_reg #(
    .FIFO_DEPTH (4),
    .FEEDTHROUGH(1'b1),
    .fifo_data_t(logic [15:0])
  ) dut (
    .clk_i,
    .rst_ni,
    .w_i,
    .wok_o,
    .wdata_i,
    .r_i,
    .rok_o,
    .rdata_o
  );

endmodule
