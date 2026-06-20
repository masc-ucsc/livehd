module \implies_ops.top (
  input        a,
  input        b,
  input        c,
  input  [3:0] x,
  input  [3:0] y,
  output       out1,
  output       out2,
  output       out3,
  output       out4
);

  // Every `implies` in the Pyrope source is hand-expanded here to `(!lhs)|rhs`.
  // out1 = a implies b
  assign out1 = (!a) | b;
  // out2 = (a implies b) implies c  ==  !((!a)|b) | c
  assign out2 = (!((!a) | b)) | c;
  // out3 = (x == 0) implies (y == 0)
  assign out3 = (!(x == 4'd0)) | (y == 4'd0);
  // out4 = (a implies b) and (x != y)
  assign out4 = ((!a) | b) & (x != y);

endmodule
