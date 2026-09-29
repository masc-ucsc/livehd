// Golden for flop_reset_matrix: every (posedge/negedge clock) x (posedge
// reset / negedge reset / synchronous reset) flop combination, each with its
// own distinct non-zero reset value. The active-high flops share the
// implicit `reset` input; the active-low (negedge-reset) flops use their own
// explicitly-named `reset_n` input (real designs never drive both polarities
// off the exact same net without an inverter in between, and `lhd lec`'s
// reset-hold phase keys the asserted level per PORT NAME -- sharing one name
// across two polarities makes that phase ambiguous, see reg_attr_override's
// `rst_n` for the same one-input-per-polarity convention).
module \flop_reset_matrix.matrix (
  input        clock,
  input        reset,
  input        reset_n,
  input        en,
  output [7:0] q_pp,
  output [7:0] q_pn,
  output [7:0] q_ps,
  output [7:0] q_np,
  output [7:0] q_nn,
  output [7:0] q_ns
);

  reg [7:0] r_pp;
  reg [7:0] r_pn;
  reg [7:0] r_ps;
  reg [7:0] r_np;
  reg [7:0] r_nn;
  reg [7:0] r_ns;
  assign q_pp = r_pp;
  assign q_pn = r_pn;
  assign q_ps = r_ps;
  assign q_np = r_np;
  assign q_nn = r_nn;
  assign q_ns = r_ns;

  // posedge clk, async active-high reset (posedge reset), reset value = 1
  always @(posedge clock or posedge reset) begin
    if (reset) r_pp <= 8'd1;
    else if (en && r_pp < 8'd200) r_pp <= r_pp + 8'd1;
  end

  // posedge clk, async active-low reset (negedge reset_n), reset value = 2
  always @(posedge clock or negedge reset_n) begin
    if (!reset_n) r_pn <= 8'd2;
    else if (en && r_pn < 8'd200) r_pn <= r_pn + 8'd1;
  end

  // posedge clk, synchronous active-high reset, reset value = 3
  always @(posedge clock) begin
    if (reset) r_ps <= 8'd3;
    else if (en && r_ps < 8'd200) r_ps <= r_ps + 8'd1;
  end

  // negedge clk, async active-high reset (posedge reset), reset value = 4
  always @(negedge clock or posedge reset) begin
    if (reset) r_np <= 8'd4;
    else if (en && r_np < 8'd200) r_np <= r_np + 8'd1;
  end

  // negedge clk, async active-low reset (negedge reset_n), reset value = 5
  always @(negedge clock or negedge reset_n) begin
    if (!reset_n) r_nn <= 8'd5;
    else if (en && r_nn < 8'd200) r_nn <= r_nn + 8'd1;
  end

  // negedge clk, synchronous active-high reset, reset value = 6
  always @(negedge clock) begin
    if (reset) r_ns <= 8'd6;
    else if (en && r_ns < 8'd200) r_ns <= r_ns + 8'd1;
  end

endmodule
