module trivial1 (
  output reg y,
  input a, b, c,
);

wire [1:0] t;

always @(*) begin
  t = a ^ b;
  y = ((a & b) & c) | ((a & b) & ~c);
end
endmodule
