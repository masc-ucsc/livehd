// :test: roundtrip
// A partial write at a DYNAMIC bit position after another partial write of the
// same clocked memory word in one cycle. The dynamic splice used to be plain
// bit logic around a read of the COMMITTED word, so with both writes in one
// cycle on one word the `[2:0]` bits were lost, and the reader refused the
// second site (`unsupported-mem-partial-write`). It is now the read -> range
// set_mask -> store chain upass.tolg merges with the earlier writes of the
// cycle, like a constant position (mem_double_partial_write.v).
// inou/prp/tests/equiv/mem_dynamic_splice_after_write proves these merges
// against an independently written Pyrope side.
module mem_dynamic_partial_write (
    input  logic       clk,
    input  logic [1:0] a,
    input  logic [1:0] b,
    input  logic [2:0] i,
    input  logic       x,
    input  logic [7:0] din,
    output logic [7:0] q
);

  logic [7:0] mem[4];
  always_ff @(posedge clk) begin
    mem[a][2:0] <= din[2:0];
    mem[b][i]   <= x;
  end
  assign q = mem[1];

endmodule
