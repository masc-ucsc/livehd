// Golden for clock_identity.prp: the synchronizer runs on dst_clk.
module clock_identity(input dst_clk, input dst_rst, input src_bit, input src_clk, output dst_bit);
  reg [1:0] sync;
  always @(posedge dst_clk)
    if (dst_rst) sync <= 2'd0;
    else sync <= {sync[0], src_bit};
  assign dst_bit = sync[1];
endmodule
