// Golden for spec_memory_partial_merge_none: nonblocking writes in program order
// (whole, then two overlapping slices) merge on the entry; the user read is x
// on the written lanes when its address is written this cycle (ordering="none").
module spec_memory_partial_merge_none(input clk, input wf, input we, input wh,
                                      input [1:0] a, input [1:0] b, input [15:0] d,
                                      input [7:0] x, input [7:0] y, input [1:0] ra,
                                      output [15:0] q);
  reg [15:0] m [0:3];
  always @(posedge clk) begin
    if (wf) m[a] <= d;
    if (we) m[a][7:0] <= x;
    if (wh) m[b][11:4] <= y;
  end
  // The window is undefined only on the lanes a colliding write drives (the
  // memory's `undef` plane is per written lane): whole entry for `wf`, [7:0] for
  // `we`, [11:4] for `wh`. Every other bit of a colliding read is the committed one.
  wire [15:0] xm = ((wf && a == ra) ? 16'hffff : 16'h0000)
                 | ((we && a == ra) ? 16'h00ff : 16'h0000)
                 | ((wh && b == ra) ? 16'h0ff0 : 16'h0000);
  integer i;
  reg [15:0] qq;
  always @* begin
    qq = m[ra];
    for (i = 0; i < 16; i = i + 1) if (xm[i]) qq[i] = 1'bx;
  end
  assign q = qq;
endmodule
