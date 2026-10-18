// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// An instance output bound to a bit of a port vector (`.v(v_o[i])`) whose own input reads that same bit back
// (`.y(rdy & v_o[i])`), a valid/ready handshake. With a one-element vector the net is split into a mut
// accumulator written by the output binding; the input connection is read BEFORE the instance runs, so it must
// see the resolved wire (the instance's output), not the accumulator's still-unknown initial value
// (`unresolved-ref ... v_o__wtmp`, an `?` in the netlist; bp_processor's bp_me_cache_slice with one L2 bank).
// :test: lec_no_x
module feedback_child (
    input  clk,
    input  y,
    input  a,
    output v
);
  logic r;
  always_ff @(posedge clk) r <= y;
  assign v = r | a;
endmodule

module instance_output_feedback_bit (
    input  clk,
    input  rdy,
    input  a,
    output logic [0:0] v_o
);
  for (genvar i = 0; i < 1; i++) begin : b
    feedback_child s (
        .clk(clk),
        .v(v_o[i]),
        .y(rdy & v_o[i]),
        .a(a)
    );
  end
endmodule
