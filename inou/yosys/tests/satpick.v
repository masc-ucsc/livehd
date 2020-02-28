module  satpick(input [2:0] a, input [2:0] b,
  input  [2:0]c,
 
  output  [2:0] out
);

   wire   out_1;
   //wire   out_2;
   //wire   out_3;
   
  assign out_1 = a + b;
  //assign out_2 = a - c;
  //assign out_3 = a + b;
  //assign ei = - a - b;
   wire  as = a;
   wire  bs = b;

 
  assign out = out_1+as * bs - as;
  

endmodule

