/*
*/
module sw(input [31:0] d, output [31:0] q32, output [15:0] q16, output [31:0] q16b, output [31:0] same);
  assign q32  = {<<8{d}};            // 4 bytes reversed
  assign q16  = {<<8{d[15:0]}};      // 2 bytes reversed
  assign q16b = {<<16{d}};           // 2 halfwords reversed
  assign same = {>>8{d}};            // order preserved -> unchanged
endmodule
