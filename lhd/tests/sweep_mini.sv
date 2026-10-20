// compile.flatten / compile.sweep fixture (lhd_sweep_test.sh): a stub endpoint
// feeding a two-stage buffer chain next to a buffer chain fed by a real input.
//   STUB = 0  the stub never sends (valid 0): the whole first chain can only hold
//             its reset value, so after flatten + sweep its 18 flop bits go
//   STUB = 1  the stub is live (valid = the `go` input): nothing may be removed
//   STUB = 2  the stub's valid is an undriven output (x): constants fold only
//             once `compile.unknown_zero` reads the x as 0
// `lost_q` is a counter nothing reads: only the state sweep can drop it (its
// next state is its own value plus one, so per-node dead code elimination keeps it).
module sweep_stub #(parameter int STUB = 0) (
  input  logic       clk,
  input  logic       go,
  output logic [7:0] data_o,
  output logic       valid_o,
  input  logic       ready_i
);
  assign data_o = 8'h5a;
  if (STUB == 0) begin : g_idle
    assign valid_o = 1'b0;
  end else if (STUB == 1) begin : g_live
    assign valid_o = go;
  end
endmodule

module sweep_buf (
  input  logic       clk, rst_n,
  input  logic       valid_i,
  input  logic [7:0] data_i,
  output logic       ready_o,
  output logic       valid_o,
  output logic [7:0] data_o,
  input  logic       ready_i
);
  logic       full_q;
  logic [7:0] data_q;
  assign ready_o = !full_q || ready_i;
  assign valid_o = full_q;
  assign data_o  = data_q;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) full_q <= 1'b0;
    else if (valid_i && ready_o) full_q <= 1'b1;
    else if (ready_i) full_q <= 1'b0;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) data_q <= 8'h00;
    else if (valid_i && ready_o) data_q <= data_i;
endmodule

module sweep_mini #(parameter int STUB = 0) (
  input  logic       clk, rst_n,
  input  logic       go,
  input  logic       ext_valid,
  input  logic [7:0] ext_data,
  input  logic       sink_ready,
  output logic       a_valid,
  output logic [7:0] a_data,
  output logic       b_valid,
  output logic [7:0] b_data
);
  logic       s_valid, s_ready, a1_valid, a1_ready, b1_ready;
  logic [7:0] s_data, a1_data;
  logic [3:0] lost_q;
  sweep_stub #(.STUB(STUB)) s0 (.clk, .go, .data_o(s_data), .valid_o(s_valid), .ready_i(s_ready));
  sweep_buf a1 (.clk, .rst_n, .valid_i(s_valid), .data_i(s_data), .ready_o(s_ready), .valid_o(a1_valid), .data_o(a1_data), .ready_i(a1_ready));
  sweep_buf a2 (.clk, .rst_n, .valid_i(a1_valid), .data_i(a1_data), .ready_o(a1_ready), .valid_o(a_valid), .data_o(a_data), .ready_i(sink_ready));
  sweep_buf b1 (.clk, .rst_n, .valid_i(ext_valid), .data_i(ext_data), .ready_o(b1_ready), .valid_o(b_valid), .data_o(b_data), .ready_i(sink_ready));
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) lost_q <= 4'h0;
    else lost_q <= lost_q + 4'h1;
endmodule
