module struct_uflop(
  input signed clk2
  ,input [2:0] a
  ,input signed enable
  ,input signed load
  ,output reg [2:0] out
);

reg [2:0] flop_next;
reg [2:0] flop_q;

always @(*) begin
  if (load) begin
    flop_next = a;
  end else if (enable) begin
    flop_next = flop_q + 1;
  end else begin
    flop_next = flop_q;
  end

  out = flop_next;
end

always @(posedge clk2) begin
   flop_q <= flop_next;
end

endmodule
