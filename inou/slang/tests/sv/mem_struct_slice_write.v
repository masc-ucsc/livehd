// A bit-slice write into ONE ENTRY of a struct-element (tuple) memory.
//
// Such a memory has no aggregate net after upass.detuple splits it into one
// scalar memory per field (`mem.hi`, `mem.lo`), so the write has to be
// decomposed across every OVERLAPPED field. It used to take the chunked
// write-enable path instead, which emits a store naming the aggregate — a net
// that no longer exists — and the write was silently DROPPED: no diagnostic,
// and hardware that never updates the memory.
//
// This is pinned as an EMISSION check rather than a LEC: a split tuple memory
// has two memory cells where the Verilog reference has one array, and the
// lgyosys miter cannot pair their (free) initial contents, so it refutes even
// for a whole-element write that has always lowered correctly.
typedef struct packed {
  logic [3:0] hi;
  logic [3:0] lo;
} pair_t;

module mem_struct_slice_write (
    input  logic        clk,
    input  logic  [1:0] adr,
    input  logic [15:0] din,
    output logic  [7:0] onef_out,
    output logic  [7:0] span_out
);

  // Slice covering exactly one field: a plain field store, no read-back.
  pair_t onef[4];
  always_ff @(posedge clk) begin
    onef[adr][3:0] <= din[3:0];
  end
  assign onef_out = onef[1];

  // Slice CROSSING the field boundary on a two-dimensional memory: bits [5:2]
  // straddle `lo[3:2]` and `hi[1:0]`, so it becomes two field-local splices.
  pair_t span[2][2];
  always_ff @(posedge clk) begin
    span[adr[1]][adr[0]][5:2] <= din[11:8];
  end
  assign span_out = span[1][1];

endmodule
