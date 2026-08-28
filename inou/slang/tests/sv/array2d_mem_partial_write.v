// Sub-word writes into a MULTI-DIMENSIONAL clocked unpacked array (a memory),
// the stateful twin of tests/sv/array2d_partial_write.v.
//
// A clocked memory cannot take the combinational read-modify-write splice that
// fixture uses: its read returns last cycle's committed word, so two same-cycle
// partial writes to one entry would clobber each other. Constant chunk-aligned
// slices lower to the memory write-enable model instead — one write port per
// chunk, merged by the `wensize` wrapper — which used to be reachable only
// through a SINGLE selector, i.e. for one-dimensional arrays. Both entries here
// are written by TWO disjoint chunk writes in the same cycle, which is exactly
// what a read-modify-write would get wrong.
module array2d_mem_partial_write (
    input  logic        clk,
    input  logic  [1:0] adr,
    input  logic [15:0] din,
    output logic [15:0] chunk_out,
    output logic  [7:0] wide_out,
    output logic  [7:0] odd_out
);

  // Element `[1:0][3:0]`: two 4-bit write-enable chunks per entry.
  logic [1:0][3:0] chunk[2][2];
  always_ff @(posedge clk) begin
    chunk[adr[1]][adr[0]][0] <= din[3:0];
    chunk[adr[1]][adr[0]][1] <= din[7:4];
  end
  assign chunk_out = {chunk[1][1], chunk[0][0]};

  // A chunk WIDER than one bit at a constant lane, with the outer dimension
  // constant and only the inner one dynamic — the mixed const/runtime selector
  // chain that build_unpacked_index folds into one linear address.
  logic [1:0][3:0] wide[2][2];
  always_ff @(posedge clk) begin
    wide[1][adr[0]][1] <= din[11:8];
  end
  assign wide_out = wide[1][1];

  // A CONSTANT slice the chunk model cannot express — 4 bits at offset 2 is
  // neither chunk-aligned nor a divisor-aligned lane of the 8-bit word. This
  // falls through to a read-modify-write of the COMMITTED entry, which is what
  // a nonblocking partial write means when it is the only write to that word.
  logic [7:0] odd[2][2];
  always_ff @(posedge clk) begin
    odd[1][adr[0]][5:2] <= din[3:0];
  end
  assign odd_out = odd[1][1];

endmodule
