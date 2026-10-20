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

// zb_lanes: two partial writes of different bit ranges of the same 16-bit entry (a
// write-masked memory: the enable of each port is a lane vector that is its condition
// on the written lanes and 0 elsewhere) and a full-word read. Without
// memory_prune_reads the lowered module is handed each lane as an independent input
// bit; with it, one condition bit per port.
module zb_lanes (
  input  logic        clk,
  input  logic        we0,
  input  logic        we1,
  input  logic [2:0]  waddr0,
  input  logic [2:0]  waddr1,
  input  logic [5:0]  wdata0,
  input  logic [3:0]  wdata1,
  input  logic [2:0]  raddr,
  output logic [15:0] r
);
  logic [15:0] mem[8];
  always_ff @(posedge clk) begin
    if (we0) mem[waddr0][6:1] <= wdata0;
    if (we1) mem[waddr1][14:11] <= wdata1;
  end
  assign r = mem[raddr];
endmodule

// zb_select: a word select by a full 32-bit index (the `of_operand1[~lane * 32 +: 32]`
// of a lane read), not the narrow affine form: the amount is `idx * 32`, whose five low
// bits are 0. As an opaque module input those barrel stages are built anyway.
module zb_select (
  input  logic [511:0] v,
  input  logic [31:0]  idx,
  output logic [31:0]  o
);
  assign o = v[idx * 32 +: 32];
endmodule

// zb_shared: two write ports with the SAME condition on the SAME lanes, so the one
// `we * lanes` enable cell feeds both ports. The memory module takes the condition bit
// for each port and the parent's enable cell, now dead, must be dropped exactly once.
module zb_shared (
  input  logic        clk,
  input  logic        we,
  input  logic [2:0]  waddr0,
  input  logic [2:0]  waddr1,
  input  logic [5:0]  wdata0,
  input  logic [5:0]  wdata1,
  input  logic [2:0]  raddr,
  output logic [15:0] r
);
  logic [15:0] mem[8];
  always_ff @(posedge clk) begin
    if (we) begin
      mem[waddr0][6:1] <= wdata0;
      mem[waddr1][6:1] <= wdata1;
    end
  end
  assign r = mem[raddr];
endmodule
