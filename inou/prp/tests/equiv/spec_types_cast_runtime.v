// Golden for spec_types_cast_runtime.prp (unsigned ports; LEC compares bit vectors).
module spec_types_cast_runtime (
   input  [3:0] a
  ,input  [3:0] b
  ,input        f
  ,output [3:0] o1
  ,output [3:0] o2
  ,output       o3
  ,output       o4
  ,output [7:0] o5
  ,output [7:0] o6
);
  assign o1 = a;                  // Signed(a): same bits
  assign o2 = b;                  // Unsigned(b): same bits
  assign o3 = f;                  // U1(f)
  assign o4 = (a != 4'd0);        // Bool(a)
  assign o5 = {4'd0, a};          // U8(a)
  assign o6 = {{4{b[3]}}, b};     // S8(b)
endmodule
