// Golden for inline_wire_output_bitsel: the adder result is a real net, so its
// slices are real logic (not X, not a folded constant).
module \inline_wire_output_bitsel.inline_wire_output_bitsel (
   input  [63:0] a,
   input  [63:0] b,
   output [47:0] x,
   output        e,
   output [63:0] y
);
  wire [63:0] s = a + b;
  assign x = s[47:0];
  assign e = s[3];
  assign y = a ^ b;
endmodule
