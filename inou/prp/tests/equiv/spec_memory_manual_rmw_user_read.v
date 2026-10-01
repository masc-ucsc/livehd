// Golden for spec_memory_manual_rmw_user_read: the by-hand merge reads the
// committed word (ordering="old"), then its whole-word write wins over `d`.
module spec_memory_manual_rmw_user_read(input clk, input wf, input we, input [1:0] a,
                                        input [15:0] d, input [7:0] x, input [1:0] ra,
                                        output [15:0] q);
  reg [15:0] m [0:3];
  wire [15:0] old_a = m[a];
  always @(posedge clk) begin
    if (wf) m[a] <= d;
    if (we) m[a] <= {old_a[15:8], x};
  end
  assign q = m[ra];
endmodule
