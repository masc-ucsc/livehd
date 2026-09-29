// LEC explicitly refuses flop-driven clocks, and native sim refuses to fold a
// derived clock, so roundtrip_sim pins both clock roots structurally
// (:verilog_re:) and keeps a refusal tripwire (:sim_unsupported:). The
// event-level bench writer_divclk_tb.v (64 cycles against an event reference,
// with async reset) runs only with LHD_EXTERNAL_SIM=1 (iverilog/vvp).
// :test: roundtrip_sim
// :verilog_re: always @\(posedge clk_i([^_[:alnum:]]|$)
// :verilog_re: always @\(posedge div_q([^_[:alnum:]]|$)
// :sim_unsupported: derived clock
// :top: divclk
module divclk (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic       req_i,
  output logic [1:0] state_o,
  output logic       div_o
);
  logic       div_q;
  logic [1:0] state_q, state_d;
  logic       new_req;

  // div_q is a REG used as a clock -> `clock_pin=ref div_q`.
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) div_q <= 1'b0; else div_q <= ~div_q & (new_req | req_i);
  end

  always_ff @(posedge div_q or negedge rst_ni) begin
    if (!rst_ni) state_q <= 2'd0; else state_q <= state_d;
  end

  always_comb begin
    state_d = state_q;
    case (state_q)
      2'd0:    if (req_i) state_d = 2'd1;
      2'd1:    state_d = 2'd2;
      default: state_d = 2'd0;
    endcase
  end

  always_comb new_req = (state_q == 2'd0) && (state_d != 2'd0);

  assign state_o = state_q;
  assign div_o   = div_q;
endmodule
