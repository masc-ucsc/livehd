// :test: roundtrip
// :top: icg_order
// A real ICG cell (latched enable, glitch-free), as designs actually
// instantiate it -- the shape the Clock_cell recognizer handles at the `Sub`
// boundary, and the shape that makes `clkgt` a wire in the emitted Pyrope.
module clkgate(input clk_i, input en_i, output clk_o);
  logic en_latch;
  always_latch if (!clk_i) en_latch <= en_i;
  assign clk_o = clk_i & en_latch;
endmodule

module icg_order (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic       req_i,
  output logic [1:0] state_o,
  output logic       busy_o,
  output logic       new_req_o
);
  localparam logic [1:0] IDLE = 2'd0;
  localparam logic [1:0] RUN  = 2'd1;
  localparam logic [1:0] DONE = 2'd2;

  logic [1:0] state_q, state_d;
  logic       new_req;
  logic       clkgt;

  // The gate, and the enable cone that reads `new_req` -> `state_d`.
  clkgate u_cg (.clk_i(clk_i), .en_i(new_req | busy_o), .clk_o(clkgt));

  always_ff @(posedge clkgt or negedge rst_ni) begin
    if (!rst_ni) begin
      state_q <= IDLE;
    end else begin
      state_q <= state_d;
    end
  end

  // `state_d`'s ONLY real writes live here, AFTER the gate above.
  always_comb begin
    state_d = state_q;
    case (state_q)
      IDLE:    if (req_i) state_d = RUN;
      RUN:     state_d = DONE;
      default: state_d = IDLE;
    endcase
  end

  // Reads the FINAL state_d — order-independent in Verilog, sequential in Pyrope.
  always_comb begin
    new_req = (state_q == IDLE) && (state_d != IDLE);
  end

  assign busy_o   = (state_q != IDLE);
  assign state_o  = state_q;
  assign new_req_o = new_req;
endmodule
