// A packed 2-D reg whose word takes two DISJOINT sub-word writes of DIFFERENT
// widths, under independent enables -- minion's `ex_reg_data` shape.
//
// Memory-izing this array cannot express the pair: a per-chunk write enable
// needs one uniform, word-dividing chunk width, and two read-modify-write
// splices into one clocked word lose each other's bits on a same-cycle
// collision. The reader must keep it a flat flop bus, where the two writes are
// ordinary composing Set_masks.
module packed2d_mixed_partial_write (
    input  logic        clock,
    input  logic        en_hi,
    input  logic        en_lo,
    input  logic        en_one,
    input  logic [63:0] din0,
    input  logic [63:0] din1,
    output logic [63:0] out0,
    output logic [63:0] out1
);
  logic [1:0][63:0] regs;

  always_ff @(posedge clock) if (en_hi)  regs[0][63:40] <= din0[63:40];
  always_ff @(posedge clock) if (en_lo)  regs[0][39:0]  <= din0[39:0];
  always_ff @(posedge clock) if (en_one) regs[1]        <= din1;

  assign out0 = regs[0];
  assign out1 = regs[1];
endmodule
