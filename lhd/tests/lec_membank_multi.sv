// Regression for two same-shaped one-sided memories.  pass.abc lowers both to
// exact, name-directed DFF banks; LEC must create private source arrays first
// so the memory<->bank bridge can relate each bank without asserting a
// positional memory correspondence.
module membank_multi #(parameter int N = 8, parameter int W = 8) (
  input  logic                 clk,
  input  logic                 wr_valid,
  input  logic [$clog2(N)-1:0] wr_addr,
  input  logic [W-1:0]         wr_data0,
  input  logic [W-1:0]         wr_data1,
  input  logic [$clog2(N)-1:0] rd_addr,
  output logic [W-1:0]         rd_data0,
  output logic [W-1:0]         rd_data1
);
  logic [W-1:0] mem0[N];
  logic [W-1:0] mem1[N];
  always_ff @(posedge clk) begin
    if (wr_valid) begin
      mem0[wr_addr] <= wr_data0;
      mem1[wr_addr] <= wr_data1;
    end
  end
  assign rd_data0 = mem0[rd_addr];
  assign rd_data1 = mem1[rd_addr];
endmodule
