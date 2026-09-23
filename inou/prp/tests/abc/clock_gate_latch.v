// Latch-based integrated clock gate feeding a register (logikbench basic/icg,
// reduced; the gate is an INSTANTIATED cell as in lambdalib's la_clkicgand).
// The flop's clock is a DERIVATION of `clk`, and synthesis partitions the gate
// and the flop into different region modules.
//
// pass.synth's whole-design gate used to refute a correct netlist here and kill
// the run: it skipped the preparation `lhd lec` runs (flatten the impl-only
// regions, fold the gated clock into a flop enable), so induction refused the
// derived clock and BMC compared independent synthetic power-on values --
// `q(ref=254 impl=0)` with the clock held low, a register that is never
// clocked. With the shared preparation it proves, unbounded.
//
// (An INLINE latch+AND gate in the same module is a different shape: `lhd lec`
// itself only reaches a bounded proof there, so the gate keeps ABC.)
module clock_gate_cell (
    input  clk,
    input  te,
    input  en,
    output eclk
);
  reg en_stable;

  // ASIC-style glitch-free gate: capture the enable while the clock is low.
  always @(clk or en or te) begin
    if (~clk) en_stable <= en | te;
  end
  assign eclk = clk & en_stable;
endmodule

module clock_gate_latch #(parameter DW = 8) (
    input               clk,
    input               en,
    input      [DW-1:0] d,
    output reg [DW-1:0] q
);
  wire eclk;

  clock_gate_cell u_icg (
      .clk (clk),
      .te  (1'b0),
      .en  (en),
      .eclk(eclk)
  );

  always @(posedge eclk) q <= d;
endmodule
