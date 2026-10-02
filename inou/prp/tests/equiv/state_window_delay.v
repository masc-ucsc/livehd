// Golden for state_window_delay.prp: bedrock br_delay (Width=8, NumStages=4).
module state_window_delay (
    input  logic            clk,
    input  logic            rst,
    input  logic [7:0]      in,
    output logic [7:0]      out,
    output logic [4:0][7:0] out_stages
);
  logic [4:0][7:0] stages;
  assign stages[0] = in;
  for (genvar i = 1; i <= 4; i++) begin : gen_stages
    always_ff @(posedge clk) begin
      if (rst) stages[i] <= 8'd0;
      else stages[i] <= stages[i-1];
    end
  end
  assign out        = stages[4];
  assign out_stages = stages;
endmodule
