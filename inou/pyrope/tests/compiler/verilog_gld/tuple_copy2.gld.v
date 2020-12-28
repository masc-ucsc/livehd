module tuple_copy2 (
  input  [3:0] \inp.foo ,
  input  [7:0] \inp.bar ,
  output [8:0] out
);

  wire  [8:0] inp_bar;
  wire  [4:0] inp_foo;
  assign inp_foo = {1'b0, \inp.foo };
  assign inp_bar = {1'b0, \inp.bar };

  assign out = $signed(inp_foo ) + $signed(inp_bar);

endmodule
