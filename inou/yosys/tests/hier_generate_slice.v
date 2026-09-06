// A packed slice reached through another generate scope is a normal lvalue.
module hier_generate_slice(input [7:0] a, output [7:0] y);
  for (genvar n = 0; n < 4; n = n + 1) begin : OUTPUTS
    wire [1:0] data;
    assign y[2*n +: 2] = data;
  end
  for (genvar i = 0; i < 2; i = i + 1) begin : INPUTS
    for (genvar n = 0; n < 4; n = n + 1) begin : WORD
      assign OUTPUTS[n].data[i] = a[4*i+n];
    end
  end
endmodule
