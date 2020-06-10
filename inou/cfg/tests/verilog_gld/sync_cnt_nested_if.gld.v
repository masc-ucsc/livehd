module sync_cnt_nested_if_gld (
  input clk,
  input c1,
  input c2,
  output reg [3:0] cnt_o
);


reg [3:0] cnt_o_next;
always @ (*) begin
  if (cnt_o >= 4'd15) begin
    cnt_o_next = 0;
  end

  if (c1 == 1) begin
    cnt_o_next = cnt_o + 1;
    if (c2 == 1) begin
      cnt_o_next = cnt_o + 2;
    end
  end
end

always @ (posedge clk) begin
  cnt_o <= cnt_o_next;
end


endmodule
