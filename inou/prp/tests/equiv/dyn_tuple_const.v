// Golden for dyn_tuple_const: a runtime element select over a 4-entry
// all-constant tuple `choices = (10,20,30,40)`, `z = choices[sel]`. With
// sel:u2 the index covers exactly 0..3, so the trailing `else` arm is the
// last element (40).
module \dyn_tuple_const.csel (
  input  [1:0] sel,
  output reg [7:0] z
);
  always @(*) begin
    if (sel == 2'd0)
      z = 8'd10;
    else if (sel == 2'd1)
      z = 8'd20;
    else if (sel == 2'd2)
      z = 8'd30;
    else
      z = 8'd40;
  end
endmodule
