module firrtl_tail3 (
  input         clock,
  input  [15:0] inp,
  output [15:0] out
);

reg  [15:0] x_next;
wire [15:0] minus = x - 1;
always @ (*) begin
  if (x == 0)
    x_next = inp;
  else
    x_next = {1'b0, minus[14:0]};
end

reg [15:0] x;
always @ (posedge clock) begin
  x <= x_next;
end

assign out = x;

endmodule
