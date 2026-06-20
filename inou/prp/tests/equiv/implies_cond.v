module \implies_cond.top (
  input        req,
  input        ack,
  input  [3:0] x,
  input  [3:0] y,
  output       grant,
  output [3:0] sel,
  output       same
);

  // grant = ack implies req
  assign grant = (!ack) | req;

  // sel = ((x < y) implies ack) ? x : y, with the condition = !(x<y) | ack
  wire cond = (!(x < y)) | ack;
  assign sel = cond ? x : y;

  // same = (req implies ack) == (!req or ack)  -- a tautology, hence constant 1
  assign same = 1'b1;

endmodule
