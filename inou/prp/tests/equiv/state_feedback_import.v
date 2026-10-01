// Golden for state_feedback_import.prp, written from the specification (not
// from LiveHD's emitter): the child `st` holds a 4-bit state (sync reset to 8)
// that loads `d` when `en`, and exposes it as `q`. The top masks the request
// with that state to form the grant, and feeds the grant back as the child's
// next state whenever any request is present. The hierarchy mirrors the
// Pyrope side (instance `m` of `st`) so the state pairs by name.
module st(input clock, input rst, input en, input [3:0] d, output [3:0] q);
  reg [3:0] r;
  assign q = r;
  always @(posedge clock) begin
    if (rst) r <= 4'd8;
    else if (en) r <= d;
  end
endmodule

module top(input clock, input rst, input [3:0] req, output [3:0] g);
  wire [3:0] m_q;
  assign g = req & ~m_q;
  st m(.clock(clock), .rst(rst), .en(req != 4'd0), .d(g), .q(m_q));
endmodule
