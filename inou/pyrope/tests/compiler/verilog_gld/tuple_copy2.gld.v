module tuple_copy2 (
  input  [3:0] \inp.foo ,
  input  [7:0] \inp.bar ,
  output [8:0] out
);

  wire  [8:0] inp_bar;
  assign inp_bar = {1'b0, \inp.bar };

  assign out = $signed(\inp.foo ) + $signed(inp_bar);

endmodule
