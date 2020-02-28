
              module simple_weird2(input a
,input b
      ,   output [1:0] z
  )  ;

  assign z[0] = a|b;
  assign z[1] = a&b;

    endmodule    

