// Golden for spec_memory_nil_no_reset: two plain memories, no reset.
module spec_memory_nil_no_reset(input clk, input we, input [1:0] a, input signed [7:0] d,
                                input [1:0] ra, output signed [7:0] q1, output signed [7:0] q2);
  reg signed [7:0] m1 [0:3];
  reg signed [7:0] m2 [0:3];
  always @(posedge clk) begin
    if (we) begin m1[a] <= d; m2[a] <= d; end
  end
  assign q1 = m1[ra];
  assign q2 = m2[ra];
endmodule
