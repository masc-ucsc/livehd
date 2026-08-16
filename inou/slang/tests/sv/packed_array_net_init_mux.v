// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// The firtool lane-table shape: a combinational packed 2-D array whose ONLY
// driver is a net initializer holding one concat lane per element, read at a
// runtime index. Splitting it turns a barrel shift over the packed bus into a
// lane mux; leaving it flat is what dino's ALU used to pay (a 1024-bit OR
// tower feeding `>> (aluop*64)`).
module packed_array_net_init_mux(
    input  logic [ 1:0] sel_i,
    input  logic [ 1:0] wsel_i,
    input  logic [15:0] a_i,
    input  logic [15:0] b_i,
    input  logic [15:0] c_i,
    input  logic [15:0] d_i,
    input  logic        clk_i,
    input  logic [31:0] din_i,
    output logic [15:0] pick_o,
    output logic [15:0] rng_o
);

  // Net initializer, per-element lanes, runtime read -> splits.
  wire [3:0][15:0] lanes = {d_i, c_i, b_i, a_i};
  assign pick_o = lanes[sel_i];

  // A write through a whole-array RANGE select has no element leaf and no
  // memory write port; only the flat bus lowers it. Must stay flat.
  logic [3:0][15:0] rng;
  always_ff @(posedge clk_i) rng[2:1] <= din_i;
  assign rng_o = rng[wsel_i];

endmodule
