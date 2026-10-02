// Golden for state_window_valid_next.prp: bedrock br_delay_valid_next
// (Width=4, NumStages=1). stage[1] loads only while stage_valid_next[1] is set.
module state_window_valid_next (
    input  logic            clk,
    input  logic            rst,
    input  logic            in_valid_next,
    input  logic [3:0]      in,
    output logic            out_valid_next,
    output logic [3:0]      out,
    output logic [1:0]      out_valid_next_stages,
    output logic [1:0][3:0] out_stages
);
  logic [1:0]      stage_valid_next;
  logic [1:0][3:0] stage;
  assign stage_valid_next[0] = in_valid_next;
  assign stage[0]            = in;
  always_ff @(posedge clk) begin
    if (rst) stage_valid_next[1] <= 1'b0;
    else stage_valid_next[1] <= stage_valid_next[0];
  end
  always_ff @(posedge clk) begin
    if (stage_valid_next[1]) stage[1] <= stage[0];
  end
  assign out_valid_next        = stage_valid_next[1];
  assign out                   = stage[1];
  assign out_valid_next_stages = stage_valid_next;
  assign out_stages            = stage;
endmodule
