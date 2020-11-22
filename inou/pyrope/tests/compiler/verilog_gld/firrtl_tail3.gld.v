module firrtl_tail3 (
  input        clock,
  input  [3:0] inp,
  output [3:0] out
);

reg  [3:0] x_next;
always @ (*) begin
  if (x == 0)
    x_next = {inp};
  else
    x_next = $signed(x) - $signed(2'b01);
end

reg [3:0] x;
always @ (posedge clock) begin
  x <= x_next;
end

assign out = x;

endmodule
