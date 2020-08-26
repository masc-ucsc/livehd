module not_gld (
  input        a,
  input  [2:0] b,
  output       o1,
  output [2:0] o2
);

assign o1 = ~a;
assign o2 = ~b;

endmodule 
