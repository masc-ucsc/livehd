module reg_bits_set (
  input        clock,
  input        reset,
  input  [7:0] a, 
  output [7:0] out
);

wire [7:0] ff_next = ff == 0 ? a : ff - 1;
reg [7:0] ff;
always @ (posedge clock) begin 
  if (reset) begin
    ff <= 'd0;
  end else begin
    ff <= ff_next;
  end
end
endmodule
