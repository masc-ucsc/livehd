
module arith (input [7:0] a, input [7:0] b,
  output [7:0] usum,
  output [7:0] ssum,
  output [7:0] usub,
  output [7:0] ssub,
  output [7:0] umul,
  output [7:0] smul,
  output [7:0] udiv,
  output [7:0] sdiv,
  output [7:0] umod,
  output [7:0] smod,
  output [7:0] upow,
  output [7:0] spow
);


  assign usum =          a + b;
  assign ssum = $signed(a) + $signed(b);

  assign usub =          a - b;
  assign ssub = $signed(a) - $signed(b);

  assign umul =          a * b;
  assign smul = $signed(a) * $signed(b);

  assign udiv =          a / b;
  assign sdiv = $signed(a) / $signed(b);

  assign umod =          a % b;
  assign smod = $signed(a) % $signed(b);

//  assign upow =          a **b;
//  assign spow = $signed(a) **$signed(b);

endmodule
