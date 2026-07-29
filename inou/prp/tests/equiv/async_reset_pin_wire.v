// FIXME tracker -- a flop's async reset pin driven by an internal WIRE is left
// undriven in the emitted Verilog (LEC-refuted). `wire CLRn = ~CLR;` used as
// `negedge CLRn` reads back correctly into Pyrope (`reset_pin=ref CLRn` plus
// `CLRn = (~CLR) & 1`, both present below), but cgen does not wire the reset pin to
// that net -- only a top-level input port reaches it -- so the reset never fires.
// The clock-gating path already handles a derived net here; the reset path does not.
module async_reset_pin_wire (
  input  CLK,
  input  CLR,
  input  D,
  output Q
);
  reg  r;
  wire CLRn = ~CLR;
  assign Q = r;
  always @(posedge CLK or negedge CLRn) begin
    if (!CLRn)
      r <= 1'b0;
    else
      r <= D;
  end
endmodule
