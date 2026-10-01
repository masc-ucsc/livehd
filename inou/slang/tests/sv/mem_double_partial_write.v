// :test: roundtrip
// TWO same-cycle sub-word writes into ONE word of a clocked memory, at a
// granularity the per-chunk write-enable model cannot express (3 bits do not
// divide the 8-bit word).
//
// Each one lowers to a read-modify-write of the addressed entry. The reader
// used to refuse the second: the read returned the COMMITTED word, so whichever
// write port won the same-cycle collision discarded the other's bits. upass.tolg
// now gives a partial write's read the earlier writes of the cycle, so the two
// merge like the nonblocking slices; this pins that the pair compiles and
// round-trips through the Pyrope writer (`mem[a]#[lo..=hi] = v`).
// inou/prp/tests/equiv/mem_partial_write_mixed checks such merges against an
// independently written Pyrope side; mem_dynamic_partial_write.v is the same
// pair with a dynamic position.
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
