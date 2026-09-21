// Behavioral oracle: div_q toggles when req_i is high. The state machine
// advances on its rising edges, never directly on every clk_i edge.
module tb;
  reg clk_i = 0, rst_ni = 1, req_i = 0;
  wire [1:0] state_o;
  wire div_o;
  reg expected_div;
  reg [1:0] expected_state;
  divclk dut(.*);
  always @(posedge clk_i or negedge rst_ni)
    if (!rst_ni) expected_div <= 0;
    else expected_div <= !expected_div && req_i;
  always @(posedge expected_div or negedge rst_ni)
    if (!rst_ni) expected_state <= 0;
    else case (expected_state)
      0: if (req_i) expected_state <= 1;
      1: expected_state <= 2;
      default: expected_state <= 0;
    endcase
  task check;
    if (state_o !== expected_state || div_o !== expected_div)
      $fatal(1, "derived-clock mismatch state=%d/%d div=%b/%b", state_o, expected_state, div_o, expected_div);
  endtask
  initial begin
    #1; rst_ni = 0; #1; check;
    for (integer k = 0; k < 64; k = k + 1) begin
      rst_ni = k % 17 != 0;
      req_i = k % 5 != 0;
      #1; clk_i = 1; #1; check;
      clk_i = 0; #1; check;
    end
    $finish;
  end
endmodule
