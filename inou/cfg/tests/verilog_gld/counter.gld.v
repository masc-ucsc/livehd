module counter_gld (
  input clk,
  input c1,
  output reg [3:0] cnt_o
);


reg [4:0] cnt_o_next;
always @ (*) begin
  if (c1 == 1)
    cnt_o_next = cnt_o + 1;
  else
    cnt_o_next = {1'b0, cnt_o};
end

always @ (posedge clk) begin
  cnt_o <= cnt_o_next[3:0];
end


endmodule
