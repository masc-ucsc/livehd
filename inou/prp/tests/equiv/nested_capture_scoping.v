// Golden for nested_capture_scoping.prp: FM = 0xF, G = 0x70 | 0xF = 0x7F, so
// f(x) = x ^ 0x7F ^ 0xF = x ^ 0x70; addn's N = Z.[bits] = 7 (Z:unsigned(bits=7));
// narrow adds its own Z = 1; Col.Green = 5 (explicit values: sequential).
module nested_capture_scoping (
  input  [7:0] a,
  input  [7:0] \p.v ,
  input  [2:0] \p.s ,
  output [7:0] o,
  output [7:0] q,
  output [7:0] r,
  output [7:0] e,
  output [3:0] lo,
  output [2:0] s
);
  assign o  = a ^ 8'h70;
  assign q  = a + 8'd7;
  assign r  = a + 8'd1;
  assign e  = a + 8'd5;
  assign lo = \p.v [7:4];
  assign s  = \p.s ;
endmodule
