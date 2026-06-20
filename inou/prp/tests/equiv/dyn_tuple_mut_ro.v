// Golden for dyn_tuple_mut_ro: a runtime element select over a read-only
// 4-entry all-constant `mut` tuple `choices = (5,6,7,8)`, `z = choices[sel]`.
module \dyn_tuple_mut_ro.msel (
  input  [1:0] sel,
  output reg [7:0] z
);
  always @(*) begin
    if (sel == 2'd0)
      z = 8'd5;
    else if (sel == 2'd1)
      z = 8'd6;
    else if (sel == 2'd2)
      z = 8'd7;
    else
      z = 8'd8;
  end
endmodule
