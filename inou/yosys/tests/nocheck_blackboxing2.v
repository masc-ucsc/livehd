
module nocheck_blackboxing2 (input a, input b, output c, output d);

wire tmp1;
wire tmp2;

cc1 i1(a,b,tmp1,tmp2);

wire tmp3;
wire tmp4;

cc1 i2(tmp1,tmp2,tmp3,d);

  wire tmp5;
  wire tmp6;

  assign tmp5 = tmp3 | 1;

cc2 i3(a,tmp6);

  wire tmp7;

  assign tmp7 = tmp6 ^ a;

cc2 i4(tmp7,c);

endmodule

