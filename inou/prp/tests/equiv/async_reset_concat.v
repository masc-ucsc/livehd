// Separate assignments: the simple spelling of the two-stage synchronizer.
module async_reset_concat (
  input clk,
  input clr_,
  input d,
  output reg q
);
  reg d0;
  always @(posedge clk or negedge clr_)
    if (~clr_) begin
      q <= 1'b0;
      d0 <= 1'b0;
    end else begin
      q <= d0;
      d0 <= d;
    end
endmodule
