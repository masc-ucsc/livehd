// Golden for spec_memory_negreset: active-low synchronous parallel reset to 9.
module spec_memory_negreset(input clk, input nr, input we, input [1:0] a, input [7:0] d,
                            input [1:0] ra, output [7:0] q);
  reg [7:0] mem [0:3];
  integer k;
  initial for (k = 0; k < 4; k = k + 1) mem[k] = 8'd9;
  always @(posedge clk) begin
    if (!nr) begin
      for (k = 0; k < 4; k = k + 1) mem[k] <= 8'd9;
    end else if (we) begin
      mem[a] <= d;
    end
  end
  assign q = mem[ra];
endmodule
