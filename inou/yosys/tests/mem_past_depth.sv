// Minimal non-power-of-two memories, including both address-boundary and concatenation cases:
//   m: 3 entries [0:2], a 2-bit address, so 3 names no entry;
//   o: 6 entries [1:6], a 3-bit address, so 0 and 7 name no entry (yosys OFFSET=1);
//   s: 3 entries [0:2], the address a concatenation of two 1-bit fields, so {1, 1}
//      names no entry. Both lanes must stay behind the address clamp.
// One data bit suffices to distinguish dropped writes and zeroed reads from entry-0 aliases.
// The offset case keeps the address and decoded-index widths equal, as in the original
// [3:14] fixture; wider addresses may legally refine an out-of-range X to an aliased read.
// An out-of-range write does nothing and an out-of-range read is X (IEEE 1800 7.4.6).
module mem_past_depth(input clk,
                      input we, input [1:0] wa, input [0:0] wd, input [1:0] ra, output [0:0] rd,
                      input we2, input [2:0] wa2, input [0:0] wd2, input [2:0] ra2, output [0:0] rd2,
                      input we3, input wp, input ww, input [0:0] wd3,
                      input rp, input rw, output [0:0] rd3);
  reg [0:0] m [0:2];
  always @(posedge clk) if (we) m[wa] <= wd;
  assign rd = m[ra];

  reg [0:0] o [1:6];
  always @(posedge clk) if (we2) o[wa2] <= wd2;
  assign rd2 = o[ra2];

  reg [0:0] s [0:2];
  always @(posedge clk) if (we3) s[{wp, ww}] <= wd3;
  assign rd3 = s[{rp, rw}];
endmodule
