module fluid_example (
  input clk,
  input wire in1,
  input wire in2,
  input wire in3,
  input wire in4,
  output reg E, 
  output reg F,
  output reg G,
  output reg H  
);
reg A, B, C ,D;

always @ (posedge clk) begin
  A <= in1;
  B <= in2;
  C <= in3;
  D <= in4;
  E <= ~A;
  F <= B ^ C;
  G <= ~D;
  H <= ~D;
end
endmodule
