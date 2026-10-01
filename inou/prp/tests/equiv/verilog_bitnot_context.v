// Verilog `~` under context-width extension (IEEE 1800 11.6): the operand is
// extended to the expression's CONTEXT width first, then every bit of that
// width flips. So a 1-bit `x1` assigned to 8 bits reads 8'hFF when x1 is 0, and
// a compare against a wider literal flips the zero-extended high bits too.
// The Pyrope twin (verilog_bitnot_context.prp) spells each width explicitly;
// prp-v2prp2v checks that the slang reader and the Pyrope writer keep these
// exact under Pyrope's typed `~` (ruling 26).
module verilog_bitnot_context(
  input               x1,
  input        [3:0]  a4,
  input        [3:0]  b4,
  input        [1:0]  c2,
  input signed [3:0]  s4,
  output       [7:0]  y8,
  output       [7:0]  z8,
  output              e1,
  output              e2,
  output              e3,
  output       [5:0]  cat,
  output       [7:0]  xn8,
  output signed [7:0] sy8,
  output       [7:0]  su8,
  output       [7:0]  m8
);
  assign y8  = ~x1;             // context 8: 8'hFF / 8'hFE
  assign z8  = ~a4;             // 8'hF0 | ~a4
  assign e1  = (~x1 == 1'b1);   // 1-bit compare: x1 == 0
  assign e2  = (~a4 == 4'hF);   // 4-bit compare: a4 == 0
  assign e3  = (~a4 == 8'hF0);  // 8-bit compare: a4 == 4'hF
  assign cat = {~a4, c2};       // self-determined inside a concat: 4 bits flip
  assign xn8 = a4 ~^ b4;        // context 8: the high nibble reads all ones
  assign sy8 = ~s4;             // signed: sign-extend, then flip
  assign su8 = ~s4;             // same bits into an unsigned destination
  assign m8  = ~a4 + 8'd1;      // context 8: the two's complement negate of a4
endmodule
