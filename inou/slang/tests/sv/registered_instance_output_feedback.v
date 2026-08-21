module registered_instance_output_feedback_pair (
    input  logic clk_i,
    input  logic reset_i,
    input  logic d_i,
    output logic q_lo_o,
    output logic q_hi_o
);
  logic lo_en_2p;
  logic hi_en_1p;
  logic q_lo_reg;
  logic q_hi_reg;

  always_latch begin
    if (reset_i)
      lo_en_2p <= 1'b0;
    else if (clk_i)
      lo_en_2p <= 1'b1;
  end

  always_latch begin
    if (reset_i) begin
      hi_en_1p <= 1'b0;
      q_lo_reg <= 1'b0;
    end else if (!clk_i) begin
      hi_en_1p <= 1'b1;
      if (lo_en_2p)
        q_lo_reg <= d_i;
    end
  end

  always_latch begin
    if (reset_i)
      q_hi_reg <= 1'b0;
    else if (clk_i && hi_en_1p)
      q_hi_reg <= q_lo_reg;
  end

  assign q_lo_o = q_lo_reg;
  assign q_hi_o = q_hi_reg;
endmodule

module registered_instance_output_feedback (
    input  logic clk_i,
    input  logic reset_i,
    input  logic load_i,
    input  logic data_i,
    output logic q_lo_o,
    output logic q_hi_o
);
  // The instance both reads and writes q_hi_o. Since q_hi_o is registered
  // state, its feedback into d_i is legal and must not bind to the output
  // port's temporary X initializer during SystemVerilog-to-Pyrope lowering.
  registered_instance_output_feedback_pair pair (
      .clk_i  (clk_i),
      .reset_i(reset_i),
      .d_i    (reset_i ? 1'b0 : (load_i ? data_i : q_hi_o)),
      .q_lo_o (q_lo_o),
      .q_hi_o (q_hi_o)
  );
endmodule
