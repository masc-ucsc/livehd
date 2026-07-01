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

  reg [7:0] q_pp_r;
  reg [7:0] q_pn_r;
  reg [7:0] q_ps_r;
  reg [7:0] q_np_r;
  reg [7:0] q_nn_r;
  reg [7:0] q_ns_r;
  assign q_pp = q_pp_r;
  assign q_pn = q_pn_r;
  assign q_ps = q_ps_r;
  assign q_np = q_np_r;
  assign q_nn = q_nn_r;
  assign q_ns = q_ns_r;

  // posedge clk, async active-high reset (posedge reset), reset value = 1
  always @(posedge clock or posedge reset) begin
    if (reset) q_pp_r <= 8'd1;
    else if (en && q_pp_r < 8'd200) q_pp_r <= q_pp_r + 8'd1;
  end

  // posedge clk, async active-low reset (negedge reset_n), reset value = 2
  always @(posedge clock or negedge reset_n) begin
    if (!reset_n) q_pn_r <= 8'd2;
    else if (en && q_pn_r < 8'd200) q_pn_r <= q_pn_r + 8'd1;
  end

  // posedge clk, synchronous active-high reset, reset value = 3
  always @(posedge clock) begin
    if (reset) q_ps_r <= 8'd3;
    else if (en && q_ps_r < 8'd200) q_ps_r <= q_ps_r + 8'd1;
  end

  // negedge clk, async active-high reset (posedge reset), reset value = 4
  always @(negedge clock or posedge reset) begin
    if (reset) q_np_r <= 8'd4;
    else if (en && q_np_r < 8'd200) q_np_r <= q_np_r + 8'd1;
  end

  // negedge clk, async active-low reset (negedge reset_n), reset value = 5
  always @(negedge clock or negedge reset_n) begin
    if (!reset_n) q_nn_r <= 8'd5;
    else if (en && q_nn_r < 8'd200) q_nn_r <= q_nn_r + 8'd1;
  end

  // negedge clk, synchronous active-high reset, reset value = 6
  always @(negedge clock) begin
    if (reset) q_ns_r <= 8'd6;
    else if (en && q_ns_r < 8'd200) q_ns_r <= q_ns_r + 8'd1;
  end

endmodule
