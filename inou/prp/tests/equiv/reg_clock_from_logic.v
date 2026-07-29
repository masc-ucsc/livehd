// FIXME tracker -- a flop clocked by a data-merging op.
// `wire gclk = clk ^ inv; always @(posedge gclk)` is the standard programmable-
// polarity clock idiom (Xilinx RAM128X1D uses exactly it; an AES core hits the same
// guard via `or`). v2prp faithfully emits `reg q:[clock_pin=ref gclk]` with
// `gclk = clk ^ inv`, but tolg's clock whitelist admits only gating (`clk and en`)
// and inversion (`not clk`), so reloading that Pyrope raises an INTERNAL error.
// `clk ^ inv` is exactly "clk, optionally inverted" -- the whitelist should take it.
module reg_clock_from_logic (
    input  wire clk,
    input  wire inv,
    input  wire d,
    output reg  q
);
  wire gclk = clk ^ inv;
  always @(posedge gclk) q <= d;
endmodule
