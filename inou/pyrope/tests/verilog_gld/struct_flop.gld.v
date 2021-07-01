module struct_flop (
   input signed [2:0] a
  ,input signed clk2
  ,input signed enable
  ,input signed load
  ,output reg signed [2:0] out
);
reg [1:0] t_pin34_0;
reg [1:0] ___next_t_pin34_0;
reg [1:0] tmp_3;
reg signed [2:0] tmp_4;
always_comb begin
   if (enable) begin
     tmp_3 = (((2'sh1) + t_pin34_0) & (3'sh3));
   end else begin
     tmp_3 = t_pin34_0;
   end
   if (load) begin
     tmp_4 = a;
   end else begin
     tmp_4 = tmp_3;
   end
end
always_comb begin
  out = tmp_4;
  ___next_t_pin34_0 = tmp_4;
end
always @(negedge clk2 ) begin
t_pin34_0 <= ___next_t_pin34_0;
end
endmodule
