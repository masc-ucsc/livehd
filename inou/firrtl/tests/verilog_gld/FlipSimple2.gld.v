module FlipSimple2(
  input        myinput_a_ab,
  output       myinput_a_ac,
  input  [2:0] myinput_d,
  input        myinput_e,
  output       myoutput_a_ab,
  input        myoutput_a_ac,
  output [1:0] myoutput_d
);
  assign myinput_a_ac = myoutput_a_ac;
  assign myoutput_a_ab = myinput_a_ab;
  assign myoutput_d = myinput_d[1:0];
endmodule
