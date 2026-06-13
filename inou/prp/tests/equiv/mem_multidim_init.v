module \mem_multidim_init.mdi (
  input            clock,
  input            reset,
  input            i,
  input            j,
  output     [3:0] z
);

  reg [3:0] data [0:3];

  initial begin
    data[0] = 4'd1; data[1] = 4'd2; data[2] = 4'd3; data[3] = 4'd4;
  end

  always @(posedge clock) begin
    if (reset) begin
      data[0] <= 4'd1; data[1] <= 4'd2; data[2] <= 4'd3; data[3] <= 4'd4;
    end
  end

  // async read of the row-major flat address i*2 + j
  assign z = data[{i, j}];

endmodule
