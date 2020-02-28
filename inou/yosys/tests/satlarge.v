module satlarge(input[1:0]  a, input[1:0]  b,
  input  [1:0] c,
  input  [1:0] d,
  input  [1:0] e,
  input [1:0] f,
  input [1:0] g,
  input[1:0]  h,
  output [1:0] out
);

   wire   out_1;
   wire   out_2;
   wire   out_3;
   
  assign out_1 = a + b;
  assign out_2 = a - b;
  assign out_3 = a + b - a;
  //assign ei = - a - b;

   wire  as = a;
   wire  bs = b;

 
  assign out = out_1+out_2*out_3+as + bs - as;
  

endmodule

