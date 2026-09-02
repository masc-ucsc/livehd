// pass.abc memory=true fixture (lhd_abc_memlower_test.sh): the bedrock
// "multi-writer register tile" shape — N entries x 8 bits, every entry written
// by its OWN always_ff (a CONSTANT-index write port with a per-entry enable and
// a synchronous reset to 0), read by two ports with RUNTIME addresses. slang
// lowers it to one Memory cell with 2N constant-address write ports
// (wr_addr_k = k) and 2 read ports; the same shape as
// br_fifo_shared_dynamic_flops' data/pointer tiles and br_flow_deserializer.
module memtile #(parameter int N = 32) (
  input  logic              clk,
  input  logic              rst,
  input  logic [N-1:0]      we,
  input  logic [N-1:0][7:0] wdata,
  input  logic [$clog2(N)-1:0] raddr0,
  input  logic [$clog2(N)-1:0] raddr1,
  output logic [7:0]        rdata0,
  output logic [7:0]        rdata1
);
  logic [7:0] mem[N];
  for (genvar i = 0; i < N; i++) begin : g
    always_ff @(posedge clk) begin
      if (rst) mem[i] <= '0;
      else if (we[i]) mem[i] <= wdata[i];
    end
  end
  assign rdata0 = mem[raddr0];
  assign rdata1 = mem[raddr1];
endmodule
