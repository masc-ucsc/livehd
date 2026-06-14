module tc_sram_gate (
  input  logic        clk_i,
  input  logic        rst_ni,
  input  logic        req_i,
  input  logic        we_i,
  input  logic [3:0]  addr_i,
  input  logic [31:0] wdata_i,
  input  logic [3:0]  be_i,
  output logic [31:0] rdata_o
);
  logic [0:0]       req_arr;
  logic [0:0]       we_arr;
  logic [0:0][3:0]  addr_arr;
  logic [0:0][31:0] wdata_arr;
  logic [0:0][3:0]  be_arr;
  logic [0:0][31:0] rdata_arr;

  assign req_arr[0]   = req_i;
  assign we_arr[0]    = we_i;
  assign addr_arr[0]  = addr_i;
  assign wdata_arr[0] = wdata_i;
  assign be_arr[0]    = be_i;
  assign rdata_o      = rdata_arr[0];

  tc_sram #(
    .NumWords(32'd16),
    .DataWidth(32'd32),
    .ByteWidth(32'd8),
    .NumPorts(32'd1),
    .Latency(32'd1),
    .SimInit("zeros"),
    .PrintSimCfg(1'b0),
    .ImplKey("none")
  ) dut (
    .clk_i(clk_i),
    .rst_ni(rst_ni),
    .req_i(req_arr),
    .we_i(we_arr),
    .addr_i(addr_arr),
    .wdata_i(wdata_arr),
    .be_i(be_arr),
    .rdata_o(rdata_arr)
  );
endmodule
