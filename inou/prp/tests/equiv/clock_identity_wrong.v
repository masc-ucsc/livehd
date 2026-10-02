// Golden for clock_identity_wrong.prp: the synchronizer runs on dst_clk.
module clock_identity_wrong(input dst_clk, input src_bit, input src_clk, output dst_bit);
  reg [1:0] sync;
  always @(posedge dst_clk) sync <= {sync[0], src_bit};
  assign dst_bit = sync[1];
endmodule
