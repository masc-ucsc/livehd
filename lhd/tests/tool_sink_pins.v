// Fixture for the `lhd tool cat` endpoint-labelling regression.
//
// What it has to contain, and why:
//   * a mux whose OUTPUT pin carries a name (`named_mux_q`), because the bug
//     only appeared on such a node -- `pin_name_of` on one of its SINK pins
//     handed back the OUTPUT's name, so the selector edge printed with the
//     driver's label instead of its port id;
//   * at least three distinct sink operands on that mux (selector + two data
//     arms), so a collapsed label is detectable as a COLLISION and not just as
//     an odd spelling;
//   * a constant operand (the default arm, and the `^ 8'h0f`), because
//     `tool_node_consts` labelled a constant's sink pin the same lossy way;
//   * primary inputs feeding the mux, because an edge driven by a graph input
//     was never emitted at all.
module tool_sink_pins (
    input  [1:0] sel,
    input  [7:0] a,
    input  [7:0] b,
    input  [7:0] c,
    output [7:0] q
);
  reg [7:0] named_mux_q;
  always @* begin
    case (sel)
      2'd0: named_mux_q = a;
      2'd1: named_mux_q = b;
      2'd2: named_mux_q = c;
      default: named_mux_q = 8'ha5;
    endcase
  end
  assign q = named_mux_q ^ 8'h0f;
endmodule
