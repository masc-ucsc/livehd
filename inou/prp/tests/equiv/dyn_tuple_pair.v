// Golden for dyn_tuple_pair: a 2-entry tuple-of-wires `pair = {a,b}` indexed by
// a 1-bit runtime select, `z = pair[sel]` (a plain 2:1 mux).
module \dyn_tuple_pair.sel2 (
  input        sel,
  input  [7:0] a,
  input  [7:0] b,
  output reg [7:0] z
);
  always @(*) begin
    if (sel == 1'd0)
      z = a;
    else
      z = b;
  end
endmodule
