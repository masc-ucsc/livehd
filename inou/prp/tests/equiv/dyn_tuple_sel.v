// Golden for dyn_tuple_sel: a runtime element select over a 4-entry
// tuple-of-wires `choices = {a,b,c,d}`, `z = choices[sel]`. With sel:u2 the
// index covers exactly 0..3, so the trailing `else` arm is element 3 (d).
module \dyn_tuple_sel.sel4 (
  input  [1:0] sel,
  input  [7:0] a,
  input  [7:0] b,
  input  [7:0] c,
  input  [7:0] d,
  output reg [7:0] z
);
  always @(*) begin
    if (sel == 2'd0)
      z = a;
    else if (sel == 2'd1)
      z = b;
    else if (sel == 2'd2)
      z = c;
    else
      z = d;
  end
endmodule
