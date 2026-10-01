// Golden for spec_memory_poweron_not_reset: reset by zap, NO power-on contents.
module spec_memory_poweron_not_reset(input ck, input zap, input we, input [1:0] a, input [7:0] d,
                                     input [1:0] ra, output [7:0] q);
  reg [7:0] mem [0:3];
  integer k;
  always @(posedge ck) begin
    if (zap) begin
      for (k = 0; k < 4; k = k + 1) mem[k] <= 8'd3;
    end else if (we) begin
      mem[a] <= d;
    end
  end
  assign q = mem[ra];
endmodule
