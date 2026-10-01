// Golden for generic_array_xfill_elem: a parameterized module whose unpacked
// array is driven only by continuous slice assigns (every undriven bit is x)
// and read whole and by slice.
module generic_array_xfill_elem #(parameter Depth = 2) (
  input  [31:0] a,
  input  [31:0] b,
  input  [31:0] c,
  output [63:0] o,
  output [31:0] p
);
  logic [1:0][31:0] w [Depth];

  assign w[0][0] = a;
  assign w[0][1] = b;
  assign w[1][0] = c;

  assign o = w[0];
  assign p = w[1][0];
endmodule
