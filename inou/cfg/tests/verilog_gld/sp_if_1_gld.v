module sp_if_1(a, b, out);
  input a;
  input b;
  output out;

  wire cond = a == 1'b1;
  assign out = cond ? a & b : a ^ b;

endmodule
