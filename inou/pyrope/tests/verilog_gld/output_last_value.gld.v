/* verilator lint_off WIDTH */
module output_last_value(
   output reg signed [2:0] out
  ,output reg signed [2:0] out2
);
always_comb begin
end
always_comb begin
  out = 3'sh3;
  out2 = 3'sh3;
end
endmodule
