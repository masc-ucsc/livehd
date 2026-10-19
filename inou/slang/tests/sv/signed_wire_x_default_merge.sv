// A SIGNED combinational net that an `always_comb` first fills with the don't-care
// default (`b = 'x`) and then overwrites under a condition, while it also feeds an
// instance whose ready output the same block reads. The instance is an early reader of
// `b`, so the reader splits it into a resolved wire plus a `b__wtmp` accumulator; a
// signed accumulator declares no range (its poison `0sb?` has no width to state).
// That poison used to be taken as a 65-bit signed envelope, so the conditional merge
// looked like a too-wide value written into the 44-bit declared net and the compile
// died with "`b` (unbounded range) may not fit its declared range".
// :test: lec
module sub (
    input  logic                clk,
    input  logic signed [43:0]  b_i,
    input  logic                v_i,
    output logic                rdy_o,
    output logic signed [43:0]  r_o
);
  logic r, r2;
  always_ff @(posedge clk) begin
    r   <= v_i;
    r2  <= ~r2;
    r_o <= b_i + 44'sd1;
  end
  assign rdy_o = r && r2;
endmodule

module signed_wire_x_default_merge (
    input  logic               clk,
    input  logic [2:0]         st,
    output logic signed [43:0] y,
    output logic               o
);
  typedef logic signed [43:0] t_t;
  t_t   b, acc;
  logic rdy, v;
  sub u (.clk(clk), .b_i(b), .v_i(v), .rdy_o(rdy), .r_o(acc));
  assign y = acc;
  always_comb begin
    b = 'x;
    v = 0;
    o = 0;
    if (st == 3) begin
      o = rdy;
      v = 1;
      b = (acc <= 0) ? 1 : acc;
    end
  end
endmodule
