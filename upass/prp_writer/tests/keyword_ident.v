// Regression fixture: Verilog signals named for Pyrope reserved words.
//
// Every spelling here is a keyword in prpparse/prp_keywords.def, so an
// unescaped emit produces a .prp that lhd cannot re-parse.  `stage` is the one
// that motivated the fixture -- bedrock's br_delay_valid names its shift
// register `stage`, and a bare `stage[0] = a` re-lexes as a `stage[N]`
// pipelining declaration ("expected an expression" on the `=`).
module top(input clk, input [7:0] a, output [7:0] o);
  reg [7:0] stage [0:1];
  reg [7:0] tick;
  reg [7:0] formal;

  always @(posedge clk) begin
    stage[0] <= a;
    stage[1] <= stage[0];
    tick     <= a ^ 8'h5a;
    formal   <= a & 8'h0f;
  end

  assign o = stage[1] ^ tick ^ formal;
endmodule
