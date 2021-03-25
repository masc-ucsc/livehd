module firrtl_tail2 (
  input        clock,
  input        reset,
  input        en,
  input  [3:0] inp,
  output [3:0] out
);

reg [3:0] x;
reg [3:0] x_next;
wire signed [4:0] foo = $signed(x) - 2'sh1;
always @ (*) begin
  if (en == 1)
    x_next = inp;
  else
    x_next = foo[3:0];
end

always @ (posedge clock) begin
  if (reset) begin
    x <= 'h0;
  end else begin
  x <= x_next;
  end
end
assign out = x;
endmodule
