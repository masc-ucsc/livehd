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
    input      [3:0]  we0,
    input      [1:0]  a0,
    input      [15:0] d0,
    input      [8:0]  we1,
    input      [1:0]  a1,
    input      [8:0]  d1,
    output reg [15:0] q0,
    output reg [8:0]  q1,
    output     [1:0]  y,
    output reg        qn);
  reg [15:0] m0 [0:3];
  reg [8:0]  m1 [0:3];
  integer    i;

  // write-enable width 4 (one lane per nibble)
  always @(posedge clk) begin
    for (i = 0; i < 4; i = i + 1) begin
      if (we0[i]) m0[a0][i*4 +: 4] <= d0[i*4 +: 4];
    end
    q0 <= m0[a0];
  end

  // write-enable width 9 (one lane per bit) -- a DIFFERENT width
  always @(posedge clk) begin
    for (i = 0; i < 9; i = i + 1) begin
      if (we1[i]) m1[a1][i] <= d1[i];
    end
    q1 <= m1[a1];
  end

  // THE NARROW SCALAR LANE: a 1-bit constant zero beside a 1-bit signal.
  assign y = {1'b0, sel};

  // a negedge flop, so the design needs P=2 and the memory gate runs at all
  always @(negedge clk) qn <= sel;
endmodule
