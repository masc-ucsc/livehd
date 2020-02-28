module compare (
  output reg  y,
  input [3:0] a,
  input [3:0] b
);

always @(*) begin
  y = a < b;
end
endmodule
