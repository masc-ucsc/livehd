module common_sub (
  output reg y, z,
  input a, b, c, d,
);

always @ (*) begin
  y = (a & d) | c;
  z = (a & d) ^ b;
end
endmodule
