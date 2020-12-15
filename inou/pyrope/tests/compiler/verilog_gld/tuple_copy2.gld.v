module tuple_copy2 (
  input  [3:0] \inp.foo ,
  input  [7:0] \inp.bar ,
  output [8:0] out
);

assign out = $signed(\inp.foo ) + $signed(\inp.bar );

endmodule
