// TWO same-cycle sub-word writes into ONE word of a clocked memory, at a
// granularity the per-chunk write-enable model cannot express (3 bits do not
// divide the 8-bit word).
//
// Each one alone lowers to a read-modify-write of the addressed entry, which is
// correct: the memory read returns the COMMITTED word, exactly the bits a
// nonblocking partial write has to preserve. Two of them cannot be merged —
// both write ports splice into the same committed word, so whichever wins the
// same-cycle collision discards the other's bits. This fixture pins that the
// reader DIAGNOSES the second site instead of emitting hardware that silently
// loses a write; the chunk-enable model (see array2d_mem_partial_write.v) is
// what merges disjoint partial writes, and it needs one uniform aligned width.
module mem_double_partial_write (
    input  logic       clk,
    input  logic [1:0] a,
    input  logic [7:0] din,
    output logic [7:0] q
);

  logic [7:0] mem[4];
  always_ff @(posedge clk) begin
    mem[a][2:0] <= din[2:0];
    mem[a][7:5] <= din[7:5];
  end
  assign q = mem[1];

endmodule
