

module inner(input z, output a, input y, output h);
  assign a = y & z;
  assign h = !(y&z);

endmodule

module submodule (input a, input b, output c, output d);

inner foo(.y(a),.z(b),.a(c),.h(d));

endmodule

