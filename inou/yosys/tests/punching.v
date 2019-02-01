

module punch_lower(input a1, input y, output c);
  assign c = y + a1;
endmodule

module punch_low(input z, input y, output a2, output h);
  assign h = !(y&z);

  punch_lower lower(.y(y),.a1(z),.c(a2));
endmodule

module punching1(input a3, input b, output c);

  wire d1;
  wire d2;
  wire d3;

  punch_low foo1(.y(a3),.z(b),.a2(d1),.h(d2));
  punch_lower foo2(.a1(a3),.y(~b),.c(d3));

  assign c = d1^d2^d3;

endmodule

module punching(input a4, input b, output c);

  punching1 p1(.a3(a4),.b(b),.c(c));

endmodule

