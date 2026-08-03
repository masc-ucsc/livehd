// Reference for mem_fwd_selfwrite: the SAME machine as FOUR NAMED FLOPS plus
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
  input      [7:0] a,
  input      [1:0] wsel,
  input      [1:0] rsel,
  input            we,
  output     [7:0] z
);

  reg [7:0] t0, t1, t2, t3;

  // Value currently held at the WRITE index: the self-read of the wdata cone.
  wire [7:0] cur_w = (wsel == 2'd0) ? t0 :
                     (wsel == 2'd1) ? t1 :
                     (wsel == 2'd2) ? t2 : t3;
  wire [7:0] wdata = cur_w | a;
  wire       wr    = we && !rst;   // reset wins over the write, as in the .prp

  always @(posedge clock) begin
    if (rst) begin
      t0 <= 8'd0; t1 <= 8'd0; t2 <= 8'd0; t3 <= 8'd0;
    end else begin
      if (wr && wsel == 2'd0) t0 <= wdata;
      if (wr && wsel == 2'd1) t1 <= wdata;
      if (wr && wsel == 2'd2) t2 <= wdata;
      if (wr && wsel == 2'd3) t3 <= wdata;
    end
  end

  // Value currently held at the READ index.
  wire [7:0] cur_r = (rsel == 2'd0) ? t0 :
                     (rsel == 2'd1) ? t1 :
                     (rsel == 2'd2) ? t2 : t3;

  // Reset clears the read too (the .prp writes 0 into every entry before the
  // read), then ordinary FORWARDING of a same-cycle write.
  assign z = rst ? 8'd0 : ((wr && (wsel == rsel)) ? wdata : cur_r);

endmodule
