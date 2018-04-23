module fluid_example (
  input wire in1,
  input wire in2,
  input clk,
  output reg C, 
  output reg D,
  output reg [1:0] E,
  output reg [1:0] F  
);
reg A, B;

always @ (posedge clk) begin
  A <= in1;
  B <= in2;
  C <= A ^ B;
  D <= A ^ B;
  E[0] <= ~A;
  E[1] <= ~B;
  F <= E;
end
endmodule
