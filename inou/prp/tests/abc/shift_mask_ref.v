module reference(input [7:0] data, input [15:0] amount, output [7:0] masked, shifted);
  for (genvar k=0; k<8; k=k+1) begin
    assign masked[k] = data[k] & (amount > k);
    wire [7:0] choices;
    for (genvar j=0; j<8; j=j+1) begin
      if (j<=k) assign choices[j] = data[k-j] & (amount == j);
      else assign choices[j] = 0;
    end
    assign shifted[k] = |choices;
  end
endmodule
