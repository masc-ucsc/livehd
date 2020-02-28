module cse_basic (
  output reg y, z, w, x,
  input a, b, c, d,e, 
  clk
);

reg temp1, temp2, temp3, temp4;
always @ (*) begin
  temp1 = a & d;
  temp2 = temp1 | c;
  y = temp2;
  temp3 = (a & d) ^ b;
end

always @ (posedge clk)
  z <= temp3;
endmodule

