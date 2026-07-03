// Golden for sim_sub_stateful_feedback: FALSE combinational loop through a
// STATEFUL sub-instance. `ready` is a register read; the fed-back `kill` only
// gates the register's next value, so there is no real bit-level cycle.
// Declarative Verilog and LEC handle this; lhd `--emit-dir sim:` currently
// rejects it as `comb-loop-through-instance` (flatten_false_loop_subs only
// inlines PURE-COMB callees, and this callee holds a flop).
module sssf_busy_cell(
  input  clock,
  input  reset,
  input  start,
  input  kill,
  output ready
);
  reg busy;
  always @(posedge clock) begin
    if (reset)      busy <= 1'b0;
    else if (kill)  busy <= 1'b0;
    else if (start) busy <= 1'b1;
  end
  assign ready = ~busy;
endmodule

module sim_sub_stateful_feedback_top(
  input  clock,
  input  reset,
  input  go,
  input  flush,
  output ready_o
);
  wire u_ready;
  wire kill = ~u_ready & flush;  // depends on the instance's own output
  sssf_busy_cell u(.clock(clock), .reset(reset), .start(go), .kill(kill), .ready(u_ready));
  assign ready_o = u_ready;
endmodule
