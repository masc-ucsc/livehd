// Three memories whose depth is not a power of two, read and written past their last entry:
//   m: 12 entries [0:11], a 4-bit address, so 12..15 name no entry;
//   o: 12 entries [3:14], a 4-bit address, so 0..2 and 15 name no entry (yosys OFFSET=3);
//   s: 12 entries [0:11], the address a concatenation {ptr, wid} of two 2-bit fields, so ptr 3
//      names no entry (Vortex's ipdom stack: 3 entries for each of 4 warps).
// An out-of-range write does nothing and an out-of-range read is X (IEEE 1800 7.4.6).
module mem_past_depth(input clk,
                      input we, input [3:0] wa, input [7:0] wd, input [3:0] ra, output [7:0] rd,
                      input we2, input [3:0] wa2, input [7:0] wd2, input [3:0] ra2, output [7:0] rd2,
                      input we3, input [1:0] wp, input [1:0] ww, input [7:0] wd3,
                      input [1:0] rp, input [1:0] rw, output [7:0] rd3);
  reg [7:0] m [0:11];
  always @(posedge clk) if (we) m[wa] <= wd;
  assign rd = m[ra];

  reg [7:0] o [3:14];
  always @(posedge clk) if (we2) o[wa2] <= wd2;
  assign rd2 = o[ra2];

  reg [7:0] s [0:11];
  always @(posedge clk) if (we3) s[{wp, ww}] <= wd3;
  assign rd3 = s[{rp, rw}];
endmodule
