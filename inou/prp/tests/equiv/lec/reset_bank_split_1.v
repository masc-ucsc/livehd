/*
lec_reset_bank_split_test.sh required a COMPLETED bounded proof at bound 2, not
merely "some proof": the harness accepts any PROVEN or PASS(n), so a silently
clamped bound or a different discharge path would go unnoticed.
:lec_grep: PASS\(2\) equivalent
*/
module split(input clk, input rst, input [3:0] in, output [3:0] out);
  reg stages___q_0, stages___q_1, stages___q_2, stages___q_3;
  always @(posedge clk) begin
    if (rst) begin
      stages___q_0 <= 1'b0;
      stages___q_1 <= 1'b0;
      stages___q_2 <= 1'b0;
      stages___q_3 <= 1'b0;
    end else begin
      stages___q_0 <= in[0];
      stages___q_1 <= in[1];
      stages___q_2 <= in[2];
      stages___q_3 <= in[3];
    end
  end
  assign out = {stages___q_3, stages___q_2, stages___q_1, stages___q_0};
endmodule
