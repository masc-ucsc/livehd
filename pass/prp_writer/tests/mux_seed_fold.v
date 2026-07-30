// Fixture for prp_writer_mux_decl_test.sh -- the shape that made the writer
// emit a `_mux_N` it never DECLARED.
//
// The ternary lowers (slang reader, fresh_local("mux")) to a merge temp:
//     mut _mux_1 = 2 ; if (s1) _mux_1 = 1 ; out = _mux_1
// and the trailing `if (cond)` makes `out = _mux_1` the unconditional SEED that
// the enclosing mux collapse folds into its else value. The writer then had two
// analyses claim the same store: analyze_muxes folded it away, and
// analyze_expr_inlines separately picked it as the temp's single use -- dropping
// the temp's definition while the folded store still rendered the bare name.
module mux_seed_fold (
  input        s0,
  input        s1,
  input        cond,
  output reg [1:0] out
);
  always @(*) begin
    out = 2'd0;
    if (s0) begin
      out = s1 ? 2'd1 : 2'd2;
      if (cond) out = 2'd3;
    end
  end
endmodule
