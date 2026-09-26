// A FALSE word-level loop through two pure-comb instances: the parent wires
// core.request -> arb.request and arb.can_grant -> core.can_grant, and core's
// request never depends on can_grant. arb assigns can_grant bit by bit, which
// the slang reader returns as per-element outputs that the parent reassembles
// through an or/shl chain. pass.bitwidth used to walk that chain consumer-first
// (hhds puts a Sub-closed SCC in raw storage order), resolve one link per
// iteration, and leave it unbounded past N=5; pass.abc then refused the
// netlist ("could not be materialized").
module arb #(parameter int N = 4) (input logic [N-1:0] request, output logic [N-1:0] can_grant, output logic [N-1:0] grant);
  assign can_grant[0] = 1'b1;
  for (genvar i = 1; i < N; i++) begin : g
    assign can_grant[i] = !(|request[i-1:0]);
  end
  assign grant = request & can_grant;
endmodule

module core #(parameter int N = 4) (input logic [N-1:0] push_valid, input logic pop_ready, input logic [N-1:0] can_grant,
    input logic [N-1:0] grant, output logic [N-1:0] request, output logic [N-1:0] push_ready, output logic pop_valid);
  assign request = push_valid;
  assign push_ready = {N{pop_ready}} & can_grant;
  assign pop_valid = |grant;
endmodule

module synth_false_loop_arb (input logic [7:0] push_valid, input logic pop_ready, output logic [7:0] push_ready, output logic pop_valid);
  logic [7:0] request, can_grant, grant;
  arb  #(.N(8)) a (.request, .can_grant, .grant);
  core #(.N(8)) c (.push_valid, .pop_ready, .can_grant, .grant, .request, .push_ready, .pop_valid);
endmodule
