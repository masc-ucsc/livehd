// foreach over an unpacked array (slang-only construct; yosys-excluded via
// the nocheck_ prefix). Tier verilog: the foreach lowering produces an
// async-read memory whose LEC miter is inconclusive.
module slang_foreach(input [7:0] a, output [7:0] y);
  reg [7:0] arr [3:0];
  reg [7:0] acc;
  always @(*) begin
    foreach (arr[k]) begin
      arr[k] = a + k[7:0];
    end
    acc = 8'd0;
    foreach (arr[k]) begin
      acc = acc + arr[k];
    end
  end
  assign y = acc;
endmodule
