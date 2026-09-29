/*
*/
module pstruct(
  input  [2:0] a,
  input  [7:0] x,
  input  [7:0] y,
  output [7:0] r
);
  wire
    struct packed {logic [2:0] operation; logic [7:0] inputx; logic [7:0] inputy; logic [7:0] result; }
    io;
  wire       z = io.operation[2];
  wire [7:0] w = io.inputx + io.inputy;
  assign io = '{operation: a, inputx: x, inputy: y, result: w + z};
  assign r = io.result;
endmodule
