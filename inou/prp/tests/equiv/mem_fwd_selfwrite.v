// Reference for mem_fwd_selfwrite: the SAME machine as TWO NAMED FLOPS plus
// explicit read/write muxes, with NO memory array. The Pyrope side is a Memory
// cell, so the miter has to pair a memory against flop+mux logic.
//
// Everything the memory does implicitly is written out by hand here:
//   * `cur_w` is the self-read of the write index (pre-commit, program order);
//   * `cur_r` is the committed read;
//   * `z` FORWARDS -- a same-cycle write to the read index is visible now.
// The reset arm clears every entry so the post-reset state is concrete on both
// sides (see the .prp for why that is load-bearing, not decoration).
module \mem_fwd_selfwrite.rf (
  input            clock,
  input            rst,
  input      [3:0] a,
  input            wsel,
  input            rsel,
  input            we,
  output     [3:0] z
);

  reg [3:0] t0, t1;

  // Value currently held at the WRITE index: the self-read of the wdata cone.
  wire [3:0] cur_w = wsel ? t1 : t0;
  wire [3:0] wdata = cur_w | a;
  wire       wr    = we && !rst;   // reset wins over the write, as in the .prp

  always @(posedge clock) begin
    if (rst) begin
      t0 <= 4'd0; t1 <= 4'd0;
    end else begin
      if (wr && !wsel) t0 <= wdata;
      if (wr &&  wsel) t1 <= wdata;
    end
  end

  // Value currently held at the READ index.
  wire [3:0] cur_r = rsel ? t1 : t0;

  // Reset clears the read too (the .prp writes 0 into every entry before the
  // read), then ordinary FORWARDING of a same-cycle write.
  assign z = rst ? 4'd0 : ((wr && (wsel == rsel)) ? wdata : cur_r);

endmodule
