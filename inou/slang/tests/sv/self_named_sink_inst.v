// An instance named after its own module, of a module with NO OUTPUTS.
//
// `br_flow_checks_valid_data_impl br_flow_checks_valid_data_impl (...)` is legal
// SystemVerilog and bedrock-rtl's habit for its assertion-only sinks. The
// emitted LNAST is `fcall(dst=<inst>, callee=<module>, …)` with BOTH refs
// spelling the same name, so prp_writer's use counter read the CALLEE reference
// as a read of the call's own result — and a zero-output sink whose result is
// read is exactly the shape it refuses ("result 'X' of zero-output module 'X'
// is read"), which failed the whole Pyrope emission of every design containing
// one (13 lhdtrack bedrock tests).
module sink_obs (
    input logic clk,
    input logic rst,
    input logic valid
);
  // Zero outputs on purpose: nothing here drives anything.
  logic seen;
  always_ff @(posedge clk) begin
    if (rst) seen <= 1'b0;
    else if (valid) seen <= 1'b1;
  end
endmodule

module self_named_sink_inst (
    input  logic clk,
    input  logic rst,
    input  logic d,
    output logic q
);
  // Instance name == module name.
  sink_obs sink_obs (
      .clk(clk),
      .rst(rst),
      .valid(d)
  );

  always_ff @(posedge clk) begin
    if (rst) q <= 1'b0;
    else q <= d;
  end
endmodule
