// A register file whose entry 0 is hard-wired by a continuous assign while an
// always_ff writes the other entries (minion's prim_rf_2r1w_preview, Zero[0]).
// The slang reader lowers `assign rf_q[0] = '0` to a store with no process
// clock, so write PORT 0 of the Memory carries no clock_pin while the later
// ports carry clk.
module mem_clockless_port (
  input  logic       clk,
  input  logic [1:0] rd_addr_a,
  output logic [7:0] rd_data_a,
  input  logic [1:0] rd_addr_b,
  output logic [7:0] rd_data_b,
  input  logic       wr_en,
  input  logic [1:0] wr_addr,
  input  logic [7:0] wr_data
);
  logic [7:0] rf_q [4];
  for (genvar g = 0; g < 4; g++) begin : gen_rf
    if (g == 0) begin : gen_zero
      assign rf_q[g] = '0;
    end else begin : gen_entry
      always_ff @(posedge clk) begin
        if (wr_en && (wr_addr == g)) begin
          rf_q[g] <= wr_data;
        end
      end
    end
  end
  assign rd_data_a = rf_q[rd_addr_a];
  assign rd_data_b = rf_q[rd_addr_b];
endmodule
