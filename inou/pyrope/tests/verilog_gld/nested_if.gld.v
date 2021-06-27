module nested_if (
  input  [3:0] a,
  input  [3:0] b,
  input  [3:0] c,
  input  [3:0] d,
  input  [3:0] e,
  input  [3:0] f,
  output [4:0] o1, // unsigned output in pyrope
  output [5:0] o2
);

reg [3:0] x;
reg [4:0] y;

always @ (*) begin
  x = a;
  y = 5;
  if ($signed({1'b0, a}) > $signed({4'd1})) begin
    x = e;
    if ($signed({1'b0, a}) > $signed({4'd2})) begin
      x = b;
    end
    else if (($signed({1'b0, a}) + $signed({4'd1})) > $signed({4'd3})) begin
      x = c;
    end
    else begin
      x = d;
    end
    y = e;
  end
  else begin
    x = f;
  end
end

assign o1 = ({1'b0, x}) + ({1'b0, a});
assign o2 = $signed({y}) + $signed({1'b0, a});

endmodule



/* module nested_if_gld ( */
/*   input  [3:0] a, */
/*   input  [3:0] b, */
/*   input  [3:0] c, */
/*   input  [3:0] d, */
/*   input  [3:0] e, */
/*   input  [3:0] f, */
/*   output [4:0] o1, */
/*   output [4:0] o2 */
/* ); */

/* reg [3:0] x; */
/* reg [3:0] y; */


/* always @ (*) begin */
/*   x = a; */
/*   y = 5; */
/*   if (a > 1) begin */
/*     x = e; */
/*     if (a > 2) begin */
/*       x = b; */
/*     end */
/*     else if ((a + 1) > 3) begin */
/*       x = c; */
/*     end */
/*     else begin */
/*       x = d; */
/*     end */
/*     y = e; */
/*   end */
/*   else begin */
/*     x = f; */
/*   end */
/* end */

/* assign o1 = x + a; */
/* assign o2 = y + a; */

/* endmodule */

