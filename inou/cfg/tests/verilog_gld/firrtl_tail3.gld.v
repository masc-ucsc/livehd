module firrtl_tail3 (
  input        clk,
  input        en,
  input  [3:0] inp,
  output [3:0] out
);

reg [3:0] x_next;
wire [3:0] minus = x - 1;
always @ (*) begin
  if (en == 1)
    x_next = inp;
  else
    x_next = {1'b0, minus[2:0]};
end

reg [3:0] x;
always @ (posedge clk) begin
  x <= x_next;
end

assign out = x;

endmodule
