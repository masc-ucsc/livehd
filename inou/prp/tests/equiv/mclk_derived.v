module \mclk_derived.derived (
  input            clock,
  input            reset,
  input            clk_b,
  input            gate,
  input      [7:0] da,
  input      [7:0] db,
  // `output reg` with the SAME names the Pyrope side uses (`qa`/`qb`), not
  // `qa_r`/`qb_r`: state correspondence pairs tier-1 BY NAME, and a `_r` suffix
  // pushed both flops into the SPECULATIVE tier-2 signature pass -- where a
  // bounded bmc PASS is deliberately suppressed, so the pair could never settle.
  output reg [7:0] qa,
  output reg [7:0] qb
);

  // gclk is a DERIVED clock: an internal combinational signal, not a port.
  wire gclk;
  assign gclk = clk_b & gate;

  always @(posedge clock) if (reset) qa <= 8'd0; else qa <= da;
  // ASYNC reset on the gated flop, deliberately. A SYNCHRONOUS reset on a GATED
  // clock is a design bug in real hardware and does not match the Pyrope side:
  // with `gate` low through reset, `gclk` never rises, so a sync reset is
  // SWALLOWED and qb keeps its X -- while the Pyrope `reg qb = 0` resets
  // regardless of the gate. That mismatch is real (measured: qb ref=255 impl=0
  // at step 1), not a LEC limitation, so the golden spells the reset the way the
  // semantics require. `qa` above keeps its sync reset: it is on the UNGATED
  // `clock`, where nothing can swallow it.
  always @(posedge gclk or posedge reset) if (reset) qb <= 8'd0; else qb <= db;


endmodule
