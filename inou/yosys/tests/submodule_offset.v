
module submodule_offset(a, b, c, d);

  input [31:0] a, b;
  output [31:0] c, d;

  inner_mod foo(.a({a[10:1], b[0] }), .b({b[10:1], a[0]}),
                .c({c[10:1], d[0] }), .d({d[10:1], c[0]}));

endmodule


module inner_mod(a, b, c, d);

  input [10:0] a, b;
  output [10:0] c, d;

  assign c = a & b;
  assign d = a | b;

endmodule

