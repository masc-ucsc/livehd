module multiport_child(
   input  [7:0] a
  ,output       c
  ,output [6:0] d
  );

  assign c = a[0];
  assign d = a[7:1];

endmodule

module multiport(
   input  [3:0] a
  ,input  [3:0] b
  ,output       c
  ,output [3:0] d
  ,output       e
  ,output [4:0] f
);


multiport_child foo (.a({a,b}), .c(c), .d({d,e}));

wire z, y, x1, x2, x3;

multiport_child bar (.a({a,b}), .c(z), .d({y, x1, x2, x3}));

assign f = {z, y, x2, x3, x1};

endmodule


