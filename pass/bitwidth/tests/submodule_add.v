

module inner_add(input [3:0] z, input [3:0] y, output [3:0] x, output [3:0] w);
  assign x = z + y;
  assign w = z / 4'b0011;

endmodule

module submodule_add (input [3:0] a, input [3:0] b, output [3:0] c, output [3:0] d, output [3:0] e);

inner_add bar(.z(a),.y(b),.x(c),.w(d));

assign e = d + 4'b0010;

endmodule

