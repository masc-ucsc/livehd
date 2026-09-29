// pass/lec memory <-> storage-flop bank bridge fixture (lhd_lec_membank_test.sh):
// the smallest resetless 1rd/1wr register file -- N entries x W bits, one
// runtime-address write port, one combinational runtime-address read. No reset
// on purpose: a read of a never-written entry right after reset is the case the
// bridge exists for (br_ram_flops_tile, EnableReset=0). pass.abc memory=true
// bit-blasts `mem` into N storage flops `mem__mem<i>`, mapped to one DFF cell
// per bit `mem__mem<i>_<b>`; the LEC must tie the Memory's power-on array to
// those cells or every unwritten read refutes on a difference the hardware
// cannot show. BAD=1 corrupts the write address (a wrong netlist): the same
// bank shape, so the tie applies, and the write path must still refute.
module membank #(parameter int N = 8, parameter int W = 8, parameter bit BAD = 0) (
  input  logic                 clk,
  input  logic                 wr_valid,
  input  logic [$clog2(N)-1:0] wr_addr,
  input  logic [W-1:0]         wr_data,
  input  logic [$clog2(N)-1:0] rd_addr,
  output logic [W-1:0]         rd_data
);
  logic [W-1:0] mem[N];
  logic [$clog2(N)-1:0] waddr;
  assign waddr = BAD ? (wr_addr ^ 1'b1) : wr_addr;
  always_ff @(posedge clk) begin
    if (wr_valid) mem[waddr] <= wr_data;
  end
  assign rd_data = mem[rd_addr];
endmodule
