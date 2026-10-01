// Golden for spec_memory_reset_priority: parallel synchronous reset to 5 with
// priority over the write; program-order forwarding only outside reset.
module spec_memory_reset_priority(input clk, input rst, input we, input [1:0] a, input [7:0] d,
                                  input [1:0] ra, output [7:0] q);
  reg [7:0] mem [0:3];
  integer k;
  always @(posedge clk) begin
    if (rst) begin
      for (k = 0; k < 4; k = k + 1) mem[k] <= 8'd5;
    end else if (we) begin
      mem[a] <= d;
    end
  end
  assign q = (!rst && we && a == ra) ? d : mem[ra];
endmodule
