// Golden for dyn_tuple_idxexpr: a 3-entry tuple `{a,b,c}` indexed by the runtime
// expression `sel+1`. idx = sel+1 (u2), so idx is 1 or 2 for sel:u1; element 0
// (a) is unreachable but kept as a mux arm. Element 2 (c) is the else.
module \dyn_tuple_idxexpr.selx (
  input        sel,
  input  [7:0] a,
  input  [7:0] b,
  input  [7:0] c,
  output reg [7:0] z
);
  wire [1:0] idx = {1'b0, sel} + 2'd1;
  always @(*) begin
    if (idx == 2'd0)
      z = a;
    else if (idx == 2'd1)
      z = b;
    else
      z = c;
  end
endmodule
