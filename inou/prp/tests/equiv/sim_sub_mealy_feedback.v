// Golden for sim_sub_mealy_feedback: false comb loop through a STATEFUL
// MEALY sub-instance. The feedback (`kill`) reaches only the register's next
// value; `echo` is an independent comb in->out path that merely makes the
// callee non-Moore. No real bit-level cycle — declarative Verilog and LEC
// are fine; lhd `--emit-dir sim:` rejects it because the whole-callee Moore
// deferral declines (echo depends on din) and the stateful callee cannot be
// flattened.
module ssmf_cell(
  input        clock,
  input        reset,
  input        start,
  input        kill,
  input  [3:0] din,
  output       ready,
  output [3:0] echo
);
  reg busy;
  always @(posedge clock) begin
    if (reset)      busy <= 1'b0;
    else if (kill)  busy <= 1'b0;
    else if (start) busy <= 1'b1;
  end
  assign ready = ~busy;
  assign echo  = din ^ 4'd3;
endmodule

module sim_sub_mealy_feedback_top(
  input        clock,
  input        reset,
  input        go,
  input        flush,
  input  [3:0] d,
  output       ready_o,
  output [3:0] echo_o
);
  wire u_ready;
  wire kill = ~u_ready & flush;  // depends on the instance's own output
  ssmf_cell u(.clock(clock), .reset(reset), .start(go), .kill(kill), .din(d),
              .ready(u_ready), .echo(echo_o));
  assign ready_o = u_ready;
endmodule
