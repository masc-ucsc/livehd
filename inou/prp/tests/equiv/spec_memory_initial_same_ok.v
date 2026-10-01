// Golden for spec_memory_initial_same_ok: entry 0 resets (and powers on) to 1,
// entry 1 to 2.
module spec_memory_initial_same_ok(input clk, input rst, input we, input a, input [7:0] d,
                                   input ra, output [7:0] q);
  reg [7:0] mem [0:1];
  initial begin mem[0] = 8'd1; mem[1] = 8'd2; end
  always @(posedge clk) begin
    if (rst) begin
      mem[0] <= 8'd1;
      mem[1] <= 8'd2;
    end else if (we) begin
      mem[a] <= d;
    end
  end
  assign q = mem[ra];
endmodule
