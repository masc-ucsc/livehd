module FlipSimple2(
  input        myinput_a_a_0,
  output       myinput_a_b_0,
  input  [2:0] myinput_b,
  input        myinput_c,
  output       myoutput_a_a_0,
  input        myoutput_a_b_0,
  output [1:0] myoutput_b
);
  assign myinput_a_b_0 = myoutput_a_b_0;
  assign myoutput_a_a_0 = myinput_a_a_0;
  assign myoutput_b = myinput_b[1:0];
endmodule
