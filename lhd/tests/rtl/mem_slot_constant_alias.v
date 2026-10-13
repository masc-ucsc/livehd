// A shared integer ZERO used BOTH in a narrow scalar Concat lane and (via the
// memory slot gate) alongside wide write-enable vectors of DIFFERENT widths.
//
// `gu::create_const` returns a CANONICAL SHARED pin, so a pass that stamps a
// width onto the zero it mints for a memory gate restamps the one zero the
// whole graph shares.  Here that zero also sits in `y`'s 1-bit MSB lane, and
// the two memories have write-enable widths 4 and 9, so a stamping gate makes
// the lane 4 bits and then 9 bits wide while its declared window stays 1 --
// exactly the pass.bitwidth Concat refusal seen on B03 and B11.
module mem_slot_constant_alias(
    input             clk,
    input             sel,
    input      [15:0] we0,
    input      [1:0]  a0,
    input      [15:0] d0,
    input      [26:0] we1,
    input      [1:0]  a1,
    input      [26:0] d1,
    output reg [15:0] q0,
    output reg [26:0] q1,
    output     [32:0] y,
    output reg        qn);
  reg [15:0] m0 [0:3];
  reg [26:0] m1 [0:3];
  integer    i;

  // write-enable width 16 (one lane per bit)
  always @(posedge clk) begin
    for (i = 0; i < 16; i = i + 1) begin
      if (we0[i]) m0[a0][i] <= d0[i];
    end
    q0 <= m0[a0];
  end

  // write-enable width 27 (one lane per bit) -- a DIFFERENT, WIDER width
  always @(posedge clk) begin
    for (i = 0; i < 27; i = i + 1) begin
      if (we1[i]) m1[a1][i] <= d1[i];
    end
    q1 <= m1[a1];
  end

  // THE NARROW CONCAT LANE: a 9-bit constant-zero lane beside a 24-bit driver,
  // the same shape as B11's node 3600 (`{driver_24, 9'b0}`).  Both memory
  // write-enable widths (16 and 27) are WIDER than this 9-bit window, so a gate
  // that restamps the shared zero makes this lane's driver too wide for its
  // declared window and pass.bitwidth refuses the design.
  assign y = {q1[23:0], 9'b0};

  // a negedge flop, so the design needs P=2 and the memory gate runs at all
  always @(negedge clk) qn <= sel;
endmodule
