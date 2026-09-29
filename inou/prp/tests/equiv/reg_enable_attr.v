// Golden for reg_enable_attr: a flop whose write enable is the AND of an
// attribute-supplied condition (`g`) and the condition of the `if` that guards
// the write (`s`). Reset has priority over both.
module \reg_enable_attr.gate (
  input        clk,
  input        rst,
  input        g,
  input        s,
  input  [7:0] d,
  output [7:0] q
);
  reg [7:0] r;
  assign q = r;

  always @(posedge clk) begin
    if (rst)
      r <= 8'd0;
    else if (g && s)
      r <= d;
  end
endmodule
