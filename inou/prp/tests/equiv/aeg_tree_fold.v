// Golden for aeg_tree_fold: every output written from the ports.
module top (
    input  [7:0] a,
    input  [7:0] b,
    input  [7:0] c,
    input  signed [7:0] s,
    output [10:0] x,
    output [11:0] y,
    output [8:0] z,
    output signed [9:0] p,
    output [10:0] m,
    output signed [17:0] n,
    output e,
    output [8:0] w
);
  assign x = ({3'b000, a} + {3'b000, b}) * 11'd3;
  assign y = ({4'b0000, b} + {4'b0000, a}) * 12'd5;
  assign z = ({1'b0, a} + {1'b0, b}) ^ {1'b0, c};
  assign p = 10'sd0;
  assign m = {a, 3'b000};
  assign n = {{10{s[7]}}, s} * 18'sd1024;
  assign e = 1'b1;
  assign w = {1'b0, a} + {1'b0, c};
endmodule
