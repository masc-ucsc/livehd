

module random_delay (
  input  logic            clk,

  input  logic            in_valid,
  input  logic [32-1:0]   a_i,
  input  logic [32-1:0]   b_i,

  output logic [32-1:0]   out
);
  logic [32-1:0]   work_q;

  // FSM
  typedef enum logic [0:0] {IDLE=1'b0, RUN=1'b1} state_e;
  state_e state_q, state_d;

  // Registers
  logic [32-1:0] sum_d;
  logic [32-1:0] work_d;

  // Combinational helpers
  logic [32-1:0] next_work;
  logic [32-1:0] lsb_onehot;

  logic in_ready;
  assign in_ready = (state_q == IDLE);

  // LSB-onehot of current work value:
  // x & -x == x & (~x + 1) == isolate least-significant '1'
  // We compute (~(work_q - 1)) & work_q equivalently.
  assign lsb_onehot   = work_q & ~(work_q - 'd1);

  // Clear exactly one '1' bit per cycle using the classic trick:
  // x & (x-1) removes the least-significant '1' bit if any.
  assign next_work    = work_q & (work_q - 'd1);

  // Outputs
  logic [32-1:0]  sum_o;
  assign sum_o        = out;
  logic [32-1:0]   clear_o;
  assign clear_o      = work_q;
  logic clear_valid;
  assign clear_valid  = (state_q == RUN) || (in_valid && (sum_d != '0));
  logic [32-1:0]   bit_cleared_o;
  assign bit_cleared_o= (state_q == RUN) ? lsb_onehot : '0;
  logic done_o;
  assign done_o       = (state_q == RUN) && (work_q == '0);

  // Next-state logic
  always_comb begin
    state_d = state_q;
    sum_d   = out;
    work_d  = work_q;

    case (state_q)
      IDLE: begin
        if (in_valid) begin
          sum_d  = a_i + b_i;      // flopped adder
          work_d = a_i + b_i;      // start clearing from the sum
          // If nonzero, go RUN; if zero, remain IDLE and done_o will stay low.
          if ((a_i + b_i) != '0) state_d = RUN;
        end
      end
      RUN: begin
        // Clear one bit per cycle
        work_d = next_work;
        if (next_work == '0) begin
          // Completed; return to IDLE and wait for next request
          state_d = IDLE;
        end
      end
      default: begin
        state_d = IDLE;
      end
    endcase
  end

  // Registers
  always_ff @(posedge clk) begin
    state_q <= state_d;
    work_q  <= work_d;
    if (state_d == IDLE) begin
      out   <= sum_d;
    end else begin
      out <= 0;
    end
  end

endmodule


module tb_flopped_adder_clear_ones;

  logic clk;
  logic in_valid;
  logic [32-1:0] a_i, b_i;

  logic [32-1:0] out;

  random_delay dut (
    .clk(clk),
    .in_valid(in_valid), .a_i(a_i), .b_i(b_i),
    .out(out)
  );

  // Clock
  initial clk = 0;
  always #5 clk = ~clk;

  // Simple reset
  initial begin
    in_valid = 0;
    a_i = 0;
    b_i = 0;
    repeat (3) @(posedge clk);
    in_valid = 1;
    a_i = 1;
    b_i = 30;
    repeat (1) @(posedge clk);
    in_valid = 0;
    a_i = 100;
    b_i = 101;
    repeat (20) @(posedge clk);
    $finish();
  end

  always @(posedge clk) begin
    $display("out=%x", out);
  end

endmodule
