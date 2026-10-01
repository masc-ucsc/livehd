// Golden for bitnot_roundtrip.prp: every flip width spelled explicitly.
module bitnot_roundtrip (
  input  [2:0] w,
  input  [2:0] a,
  input  [4:0] b,
  input  [3:0] s,
  input  [1:0] c,
  input  [1:0] i,
  output [8:0] o1,
  output [8:0] o2,
  output [8:0] o3,
  output [8:0] o4,
  output [8:0] o5,
  output [8:0] o6,
  output [8:0] o7,
  output [8:0] o8,
  output [8:0] o9,
  output [7:0] o10,
  output [7:0] o11,
  output [7:0] o12,
  output [7:0] o13,
  output [1:0] o14,
  output [7:0] o16
);
  wire [4:0] ab  = {2'b0, a} & b;
  wire [8:0] sa  = {{5{s[3]}}, s} & {6'b0, a};
  wire [2:0] nw  = ~w;
  wire [1:0] t2  = w[1:0];
  assign o1  = {7'b0, ~w[1:0]};
  assign o2  = {7'b0, ~w[1:0]};
  assign o3  = {4'b0, ~ab};
  assign o4  = {4'b0, ~ab};
  assign o5  = {5'b0, ~{1'b0, w}};
  assign o6  = ~sa;
  assign o7  = {1'b0, ~{5'b0, w}};
  assign o8  = {6'b0, ~({1'b0, c} | a)};
  assign o9  = {6'b0, ~nw};
  assign o10 = {7'b0, ~a[1]};
  assign o11 = {5'b0, ~a};
  assign o12 = {7'b0, a[i]};
  assign o13 = {7'b0, ~a[i]};
  assign o14 = ~t2;  // two `~` of one 2-bit wire: 2 bits here,
  assign o16 = ~t2;  // ...and widened to the 8-bit context here
endmodule
