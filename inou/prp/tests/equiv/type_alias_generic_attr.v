module type_alias_generic_attr (
  input clock,
  input reset,
  input signed [3:0] a,
  input signed [3:0] b,
  output signed [3:0] y
);
  // One-cycle sum, truncated to four bits, with synchronous reset to zero.
  reg signed [3:0] y_r;

  always @(posedge clock) begin
    if (reset)
      y_r <= 4'sd0;
    else
      y_r <= a + b;
  end

  assign y = y_r;
endmodule
