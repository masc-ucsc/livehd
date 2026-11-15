// Golden for aeg_tree_cse: every output written from the ports, with no shared
// subexpression, so the tree aegraph's forwarding is checked against a design it never touched.
module top (
    input  [7:0] a,
    input  [7:0] b,
    input  [7:0] c,
    output [10:0] x,
    output [11:0] y,
    output [8:0] z
);
  assign x = ({3'b000, a} + {3'b000, b}) * 11'd3;
  assign y = ({4'b0000, b} + {4'b0000, a}) * 12'd5;
  assign z = ({1'b0, a} + {1'b0, b}) ^ {1'b0, c};
endmodule
