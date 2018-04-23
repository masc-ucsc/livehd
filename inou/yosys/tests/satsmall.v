module  satsmall(input  a, input  b,
  input  c,
 
  output  out
);

   wire   out_1;
   wire   out_2;
   wire   out_3;
   
  assign out_1 = a + b;
  assign out_2 = a - c;
  assign out_3 = a + b;
  //assign ei = - a - b;
   wire  as = a;
   wire  bs = b;

 
  assign out = out_1+as * bs - as;
  

endmodule

