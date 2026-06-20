// Golden for dyn_tuple_mixed: a 4-entry tuple `{a, 7, c, d}` mixing wires with
// a comptime constant, indexed by sel:u2. Slot 1 is the constant 7.
module \dyn_tuple_mixed.selm (
  input  [1:0] sel,
  input  [7:0] a,
  input  [7:0] c,
  input  [7:0] d,
  output reg [7:0] z
);
  always @(*) begin
    if (sel == 2'd0)
      z = a;
    else if (sel == 2'd1)
      z = 8'd7;
    else if (sel == 2'd2)
      z = c;
    else
      z = d;
  end
endmodule
