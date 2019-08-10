module not_vslogicnot(input [3:0] d, input [3:0] c, output [3:0] q, output [1:0] should_be_one);

  wire [10:0] tmp1;
  wire [10:0] tmp2;
  wire [10:0] tmp3;
  wire [10:0] tmp4;

  assign tmp2 = a!=c;
  assign q[1] = &tmp2;

  assign tmp1 = a==c;
  assign q[0] = &tmp1;

  assign tmp3 = ~(a==c);
  assign q[2] = &tmp3;

  assign tmp4 = ~(a!=c);
  assign q[3] = &tmp4;

  assign should_be_one[0] = tmp1 == tmp4;
  assign should_be_one[1] = tmp2 == tmp3;

endmodule

