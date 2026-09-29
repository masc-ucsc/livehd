// Golden for latch_enable_attr: a transparent-high latch whose enable is the
// AND of an attribute-supplied condition (`g`) and the condition of the `if`
// that guards the write (`en`).
module \latch_enable_attr.gate (
  input        en,
  input        g,
  input  [7:0] d,
  output [7:0] q
);
  reg [7:0] l;
  assign q = l;

  always_latch begin
    if (g && en) l <= d;
  end
endmodule
