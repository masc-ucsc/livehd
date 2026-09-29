// Regression: a RUNTIME-indexed single-bit WRITE (`q[addr] <= 1'b1`) was
// silently DROPPED by upass/prp_writer, so `v -> prp -> v` produced a register
// that never sets a bit.
//
// The dynamic index makes slang lower the write as
//   range(%r, addr, addr)  +  set_mask(q, q, %r, 1)
// i.e. a set_mask whose MASK is a ref to a range temp, not a constant.
// write_set_mask only understood a constant mask (`mask_runs` parses the text),
// so it fell through to its "zero / unparsable mask" arm and emitted the base
// copy `q = q` -- the write, and the whole `#[addr]` lane, gone. The recompile
// then reported it as `irrelevant assignment: q is assigned to itself`, which
// is how the drop was first noticed (minion_tlb, intpipe_csr_msgs).
//
// The `[hi:lo]` twin below is the constant-mask control: it always worked.
module \dynbit_write.dynbit (
  input        clk_i,
  input        rst_ni,
  input        wen_i,
  input  [2:0] waddr_i,
  input  [3:0] wdata_i,
  output [7:0] q_o,
  output [7:0] r_o
);
  reg [7:0] valid_q;
  reg [7:0] nib_q;
  always @(posedge clk_i) begin
    if (!rst_ni) begin
      valid_q <= 8'b0;
      nib_q   <= 8'b0;
    end else if (wen_i) begin
      valid_q[waddr_i] <= 1'b1;   // runtime index -- the dropped write
      nib_q[6:3]       <= wdata_i;  // constant range -- the control
    end
  end
  assign q_o = valid_q;
  assign r_o = nib_q;
endmodule
