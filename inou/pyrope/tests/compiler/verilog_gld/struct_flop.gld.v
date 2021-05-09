module pp(
   input signed [2:0] a
  ,input signed clk2
  ,input signed enable
  ,input signed [1:0] load
  ,output reg signed [2:0] out
);
reg [1:0] t_pin34;
reg [1:0] t_pin34_next ;
reg signed [2:0] tmp_4;
always_comb begin
   if (load) begin
     tmp_4 = a;
   end else begin
     tmp_4 = (((2'sh1) + t_pin34) & (3'sh3));
   end
end
always_comb begin
  out = tmp_4;
  t_pin34_next = tmp_4;
end
always @(negedge clk2 ) begin
t_pin34 <= t_pin34_next;
end
endmodule
