module logic_gld (
  input        a,
  input  [2:0] b,
  input  [2:0] c,
  output       o1,
  output [2:0] o2,
  output [2:0] o3,
  output [2:0] o4,
  output [2:0] o5,
  output       o6,
  output       o7,
  output       o8,
  output       o9
);

assign o1 = ~a;
assign o2 = ~b;
assign o3 = c & b;
assign o4 = c | b;
assign o5 = c ^ b;
assign o6 = !a == 0 ; // o6 = !a
assign o7 = !b == 0 ; // o7 = !b
wire   t1 = a == 0;
wire   t2 = b == 0;
assign o8 = t1 && t2;
assign o9 = t1 || t2;

endmodule 
