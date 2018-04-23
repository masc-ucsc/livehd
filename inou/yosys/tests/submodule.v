

module inner(input e, input f, output g, output h);
  assign g = e & f;
  assign h = !(e&f);

endmodule

module submodule (input a, input b, output c, output d);

inner foo(.e(a),.f(b),.g(c),.h(d));

endmodule

