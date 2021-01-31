
module hier_tuple_nested_if7(input en, output reg [5:0] sum);

  always @(en) begin
    if (en) begin
      sum = 1 + 3;
    end else begin
      sum = 1 + 4;
    end
  end

endmodule
