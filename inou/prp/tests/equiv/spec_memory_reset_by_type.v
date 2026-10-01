// Golden for spec_memory_reset_by_type: clocked by ck, reset by zap; `rst` is a
// data input (the write enable).
module spec_memory_reset_by_type(input ck, input zap, input rst, input [1:0] a, input [7:0] d,
                                 input [1:0] ra, output [7:0] q);
  reg [7:0] mem [0:3];
  integer k;
  // docs 08: the initializer "is the power-on contents too (the `initial` contents)".
  initial for (k = 0; k < 4; k = k + 1) mem[k] = 8'd3;
  always @(posedge ck) begin
    if (zap) begin
      for (k = 0; k < 4; k = k + 1) mem[k] <= 8'd3;
    end else if (rst) begin
      mem[a] <= d;
    end
  end
  assign q = mem[ra];
endmodule
