// NVDLA-style concatenated reset and nonblocking data assignments.
module async_reset_concat (
  input clk,
  input clr_,
  input d,
  output reg q
);
  reg d0;
  always @(posedge clk or negedge clr_)
    if (~clr_) {q, d0} <= 2'd0;
    else       {q, d0} <= {d0, d};
endmodule
