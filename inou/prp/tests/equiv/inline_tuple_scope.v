module inline_tuple_leaf(input [7:0] a, output [7:0] q);
  wire struct packed { logic [7:0] x; logic [7:0] y; logic [7:0] z; } io;
  assign io.x = a;
  assign io.y = io.x + 8'd1;
  assign io.z = io.y ^ 8'd3;
  assign q = io.z;
endmodule

module inline_tuple_scope(input [7:0] a, input [7:0] b, output [7:0] q);
  wire struct packed { logic [7:0] a; logic [7:0] b; } io;
  wire [7:0] left_q, right_q;
  assign io.a = a;
  assign io.b = b;
  inline_tuple_leaf left (.a(io.a), .q(left_q));
  inline_tuple_leaf right (.a(io.b), .q(right_q));
  assign q = left_q ^ right_q;
endmodule
