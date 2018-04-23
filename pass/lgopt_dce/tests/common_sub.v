module common_sub (
  output reg y, z,
  input a, b, c, d,
  clk
);

reg temp1, temp2, temp3;
always @ (*) begin
  temp1 = a & d;
  temp2 = temp1 | c;
  y = temp2;
  temp3 = (a & d) ^ b;
end

always @ (posedge clk)
  z <= temp3;
endmodule
