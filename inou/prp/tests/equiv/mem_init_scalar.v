module \mem_init_scalar.regi (
  input            clock,
  input      [7:0] a,
  input      [1:0] i,
  input            we,
  output     [7:0] z
);

  reg [7:0] data[3:0];

  initial begin
    data[0] = 8'd3;
    data[1] = 8'd3;
    data[2] = 8'd3;
    data[3] = 8'd3;
  end

  always @(posedge clock) begin
    if (we) data[i] <= a;
  end

  // same index for read and write + fwd
  assign z = we ? a : data[i];

endmodule
