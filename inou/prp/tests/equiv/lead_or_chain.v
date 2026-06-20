module \lead_or_chain.top (
  input  [3:0] sel,
  input  [7:0] x,
  input  [7:0] y,
  output       out1,
  output       out2
);

  // out1 — split wires (the OPPOSITE spelling of the Pyrope chained form): if
  // the Pyrope front-end dropped any chained term, out1 diverges here.
  wire b0 = (sel == 4'd0) && (x == y);
  wire b1 = (sel == 4'd1) && (x != y);
  wire b2 = (sel == 4'd2) && (x <  y);
  wire b3 = (sel == 4'd3) && (x <= y);
  wire b4 = (sel == 4'd4) && (x >  y);
  wire b5 = (sel == 4'd5) && (x >= y);
  wire b6 = (sel == 4'd6) && (x == 8'd0);
  wire b7 = (sel == 4'd7) && (y == 8'd0);
  assign out1 = b0 | b1 | b2 | b3 | b4 | b5 | b6 | b7;

  // out2 — inline chain (the OPPOSITE spelling of the Pyrope split form).
  assign out2 = ((sel == 4'd8)  && (x == y))
              | ((sel == 4'd9)  && (x != y))
              | ((sel == 4'd10) && (x <  y))
              | ((sel == 4'd11) && (x >= y))
              | ((sel == 4'd12) && (x == 8'd0))
              | ((sel == 4'd13) && (y == 8'd0));

endmodule
