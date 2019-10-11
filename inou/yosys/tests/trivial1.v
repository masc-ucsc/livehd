module trivial1 (
  output reg y,
  input a, b, c, d 
);

always @(*) begin
  /* y = ((a & b) & c) | ((a & b) & ~c); */
  /* y = ((a & b) & c) | ((d & e) & f); */
  y = (a & b) | (c & d) ;
end
endmodule
