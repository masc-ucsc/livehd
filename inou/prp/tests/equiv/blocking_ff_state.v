// Separate clocked processes retain separate registers even with blocking writes.
module blocking_ff_state (
    input  clk,
    input  d,
    output q
);

  reg sync_0, sync_1;

  always @(posedge clk) sync_0 = d;

  always @(posedge clk) sync_1 = sync_0;

  assign q = sync_1;

endmodule
