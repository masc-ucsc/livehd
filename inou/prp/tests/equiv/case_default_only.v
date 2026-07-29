// Regression: prp_writer emitting a CONDITION-LESS `unique if`.
//
// A `case` whose only arm is `default` reads in as an if-node with a single
// child -- the else-stmts, no cond/then pair. The writer renders child0 as the
// CONDITION, so it emitted `unique if  {\n}`: no condition and no body. That
// does not re-parse ("expected '{'"), so the v -> prp -> v round trip died at
// re-read. With no conditions the default arm runs unconditionally.
module case_default_only(input clk, input [1:0] a, output reg [3:0] o);
  reg [3:0] r;
  always @(*) begin
    case (a)
      default: r = 4'b0;
    endcase
  end
  always @(posedge clk) o <= r;
endmodule
