// A partial ASYNCHRONOUS reset written as constant SLICES whose value carries
// UNKNOWN bits. The slices fold into one whole-register `initial`, and that
// fold must not lose the x bits: `SVInt::as<uint64_t>()` returns nullopt for
// any x/z-bearing constant, so a `value_or(0)` fold silently reset the whole
// slice to ZERO. `lhd lec` cannot see it -- it reads BOTH sides with slang and
// would fold the reference identically -- so roundtrip_sim pins the x bits of
// the emitted reset constants (:verilog_re:), checks the defined bits with
// native directed vectors (async_reset_unknown_slices_tb.prp), and requires the
// asynchronous sensitivity list structurally (native sim is cycle-based).
// `b` is the control: a whole-register reset never took the accumulator path
// and already kept its unknowns.
// :test: roundtrip_sim
// :verilog_re: always @\(posedge clk or posedge rst
// :verilog_not_re: always @\(posedge clk[[:space:]]*\)
// :verilog_re: (^|[^_[:alnum:]])a <= \(?[0-9]+'s?b0*[?xXzZ]{2}11\)?;
// :verilog_re: (^|[^_[:alnum:]])b <= \(?[0-9]+'s?b0*[?xXzZ]0[?xXzZ]1\)?;
module async_reset_unknown_slices (
  input  logic       clk,
  input  logic       rst,
  input  logic [7:0] d,
  output logic [3:0] a,
  output logic [3:0] b
);
  always_ff @(posedge clk or posedge rst)
    if (rst) begin
      a[3:2] <= 2'bxx;
      a[1:0] <= 2'b11;
      b      <= 4'bx0x1;
    end else begin
      a <= d[7:4];
      b <= d[3:0];
    end
endmodule
