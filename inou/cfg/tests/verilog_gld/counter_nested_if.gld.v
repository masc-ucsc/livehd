module sync_cnt_nested_if_gld (
  input clock,
  input c1,
  input c2,
  output reg [3:0] cnt_o
);


reg [4:0] cnt_o_next;
always @ (*) begin
  if (c1 == 1) begin
    cnt_o_next = cnt_o + 1;
    if (c2 == 1) begin
      cnt_o_next = cnt_o + 2;
    end else begin
      cnt_o_next = {1'b0, cnt_o};
    end
  end else begin
    cnt_o_next = {1'b0, cnt_o};
  end
end

always @ (posedge clock) begin
  cnt_o <= cnt_o_next[3:0];
end

endmodule
