module compare (
  output reg  y,
  input [7:0] a,
  input [7:0] b
);

always @(*) begin
  y = a < b;
end
endmodule
