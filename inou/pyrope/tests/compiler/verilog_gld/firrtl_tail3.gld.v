module firrtl_tail3 (
  input         clock,
  input  [3:0] inp,
  output [3:0] out
);

reg  [4:0] x_next;
wire [5:0] minus = $signed(x) - $signed(2'b01);
always @ (*) begin
  if (x == 0)
    x_next = inp;
  else
    x_next = minus[4:0];
end

reg [4:0] x;
always @ (posedge clock) begin
  x <= x_next;
end

assign out = x[3:0];

endmodule
