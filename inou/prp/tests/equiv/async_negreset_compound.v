// Golden for async_negreset_compound: the NEGEDGE half of the slang reader's
// async-reset-as-sync demotion. `rst_n` (active-low) is edge-triggered in the
// sensitivity list but guarded by a COMPOUND condition (`!rst_n || clr`), so
// no async-reset rung is extractable and the reader demotes the edge to a
// synchronous read (state is only observed after clock updates). No module
// `reset` port: the Pyrope side detects `rst_n` as the module reset, so an
// extra `reset` input would mismatch the miter's port lists.
module \async_negreset_compound.pipe (
  input        clock,
  input        rst_n,
  input        clr,
  input        en,
  input  [7:0] d,
  output [7:0] q
);
  reg [7:0] r;
  assign q = r;

  always @(posedge clock or negedge rst_n) begin
    if (!rst_n || clr)
      r <= 8'd0;
    else if (en)
      r <= d;
  end
endmodule
