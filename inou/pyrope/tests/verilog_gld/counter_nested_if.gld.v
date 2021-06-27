module counter_nested_if (
  input clock,
  input reset,
  input c1,
  input c2,
  output reg [3:0] cnt_o
);


reg [4:0] cnt_o_next;
always @ (*) begin
  if (c1 == 1) begin
    if (c2 == 1) begin
      cnt_o_next = cnt_o + 3;
    end else begin
      cnt_o_next = cnt_o + 1;
    end
  end else begin
    cnt_o_next = {1'b0, cnt_o};
  end
end

always @ (posedge clock) begin
  if (reset) begin
    cnt_o <= 'h0;
  end else begin
  cnt_o <= cnt_o_next[3:0];
  end
end

endmodule
