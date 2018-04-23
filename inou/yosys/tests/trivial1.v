module trivial1 (
  output reg y,
  input a, b, c,
);

always @(*) begin
  y = ((a & b) & c) | ((a & b) & ~c);
end
endmodule
