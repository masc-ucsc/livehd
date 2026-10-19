// pass.abc.ware_zero_bits / memory_prune_reads fixture (lhd_abc_zero_bits_test.sh).
//
// zb_shift: a word select `v[idx*8 +: 8]` over a 40-bit vector that holds just ONE
// live 8-bit lane (the operand is placed at bit 16 by a constant left shift, the
// other four lanes are 0). The barrel shifter is extracted into its own module, so
// without ware_zero_bits the mapper cannot see that four lanes are constant.
module zb_shift (
  input  logic [7:0] y,
  input  logic [2:0] idx,
  output logic [7:0] o
);
  logic [39:0] v;
  assign v = 40'(y) << 16;
  assign o = 8'(v >> (idx * 8));
endmodule

// zb_mem: two read ports that each observe a 2- or 4-bit window of a 16-bit word.
// The lowered memory module otherwise builds the whole 8:1 x 16-bit read mux for
// both ports.
module zb_mem (
  input  logic        clk,
  input  logic        we,
  input  logic [2:0]  waddr,
  input  logic [15:0] wdata,
  input  logic [2:0]  raddr0,
  input  logic [2:0]  raddr1,
  output logic [1:0]  r0,
  output logic [3:0]  r1
);
  logic [15:0] mem[8];
  always_ff @(posedge clk) if (we) mem[waddr] <= wdata;
  assign r0 = mem[raddr0][1:0];
  assign r1 = mem[raddr1][11:8];
endmodule
