// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Two halves of the packed-2-D-array REG declaration gate.
//
//  * `tbl` is a plain packed register bank, every access at a literal index.
//    It must be declared as a Pyrope ARRAY (`reg tbl:[4]u12`), not flattened
//    into one 48-bit flop that each access then bit-slices back out. The
//    selector being constant is NOT a reason to flatten: constant- and
//    runtime-indexed arrays are the same LGraph memory, and recovering
//    per-element flops from a constant-addressed one is a synthesis
//    optimization.
//
//  * `rot` carries an async reset that loads a PER-ELEMENT PATTERN ({3,2,1,0} —
//    how firtool spells an index-initialized pointer vector) AND a whole-array
//    datapath write. It stays an array, with the reset SPLIT PER ENTRY into the
//    array's own initializer: one scalar `initial` attribute on `reg x:[N]uW`
//    would BROADCAST, collapsing the pattern to its bottom lane.
//
//  * `ptr` has the same pattern reset but NO whole-array datapath write. That
//    whole-array write is what becomes the memory's `update` bus, and only the
//    update-bus lowering has anywhere to hang a reset — with per-entry writes
//    alone the reset is dropped outright. So `ptr` must stay a flat bus, which
//    does reset correctly. (Measured: as an array, xiangshan's
//    PMAEntryHandleModule, RegCacheAgeTimer_1 and RobEnqPtrWrapper all LEC
//    REFUTED on exactly this.)
module packed_array_reg_array_emit (
    input               clk,
    input               rst_i,
    input               wen_i,
    input         [1:0] waddr_i,
    input        [11:0] wdata_i,
    input         [1:0] raddr_i,
    output logic [11:0] rdata_o,
    output logic  [9:0] ptr_o,
    output logic  [9:0] rot_o
);

  // ── (1) const-indexed packed register bank -> `reg tbl:[4]u12` ────────────
  reg [3:0][11:0] tbl;
  always @(posedge clk) begin
    tbl[2'd0] <= (wen_i && waddr_i == 2'd0) ? wdata_i : tbl[2'd0];
    tbl[2'd1] <= (wen_i && waddr_i == 2'd1) ? wdata_i : tbl[2'd1];
    tbl[2'd2] <= (wen_i && waddr_i == 2'd2) ? wdata_i : tbl[2'd2];
    tbl[2'd3] <= (wen_i && waddr_i == 2'd3) ? wdata_i : tbl[2'd3];
  end

  assign rdata_o = raddr_i == 2'd0 ? tbl[2'd0]
                 : raddr_i == 2'd1 ? tbl[2'd1]
                 : raddr_i == 2'd2 ? tbl[2'd2]
                 :                   tbl[2'd3];

  // ── (2) per-element pattern async reset -> stays a flat `reg ptr:u40` ─────
  reg [3:0][9:0] ptr;
  always @(posedge clk or posedge rst_i) begin
    if (rst_i) begin
      ptr <= {10'd3, 10'd2, 10'd1, 10'd0};
    end else begin
      ptr[2'd0] <= ptr[2'd3];
      ptr[2'd1] <= ptr[2'd0];
      ptr[2'd2] <= ptr[2'd1];
      ptr[2'd3] <= ptr[2'd2];
    end
  end

  assign ptr_o = ptr[2'd0];

  // ── (3) pattern reset + whole-array datapath write -> array + split reset ──
  reg  [3:0][9:0] rot;
  wire [3:0][9:0] rot_next = {rot[2'd2], rot[2'd1], rot[2'd0], rot[2'd3]};
  always @(posedge clk or posedge rst_i) begin
    if (rst_i) begin
      rot <= {10'd3, 10'd2, 10'd1, 10'd0};
    end else begin
      rot <= rot_next;
    end
  end

  assign rot_o = rot[2'd0];

endmodule
