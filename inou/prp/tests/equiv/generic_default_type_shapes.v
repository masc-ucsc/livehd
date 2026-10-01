// Golden for generic_default_type_shapes.prp.
module generic_default_type_shapes (
  input  [7:0]        a,
  input  signed [3:0] s,
  output [3:0]        l,
  output signed [3:0] n,
  output [3:0]        h,
  output [7:0]        w
);
  assign l = a[3:0];
  assign n = s;
  assign h = a[7:4];
  assign w = a;
endmodule
