// synth.clock_gate fixture (lhd_clock_gate_test.sh). r0 and r1 share the conjunct en_a (and a synchronous reset), r2 is enabled by
// en_a alone and r3 by en_c alone: three gates on `clk` (en_a | rst over r0/r1, en_a over r2, en_c over r3), each register keeping
// only the part of its enable the gate does not carry.
module clock_gate_share(input clk, input rst, input en_a, input en_b, input en_c, input [7:0] d,
                output [7:0] q0, output [7:0] q1, output [7:0] q2, output [7:0] q3);
  reg [7:0] r0, r1, r2, r3;
  always @(posedge clk) begin
    if (rst) begin
      r0 <= 8'd0;
      r1 <= 8'd0;
    end else begin
      if (en_a & en_b) r0 <= d;
      if (en_a & en_c) r1 <= d + 8'd1;
    end
  end
  always @(posedge clk) begin
    if (en_a) r2 <= d ^ 8'h5a;
    if (en_c) r3 <= d + 8'd2;
  end
  assign q0 = r0;
  assign q1 = r1;
  assign q2 = r2;
  assign q3 = r3;
endmodule
