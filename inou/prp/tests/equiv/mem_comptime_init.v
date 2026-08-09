module \mem_comptime_init.mci (
  input            clock,
  input            reset,
  input            i,
  input            j,
  output     [3:0] z
);

  reg [3:0] t [0:3];

  initial begin
    t[0] = 4'd1; t[1] = 4'd2; t[2] = 4'd3; t[3] = 4'd4;
  end

  always @(posedge clock) begin
    if (reset) begin
      t[0] <= 4'd1; t[1] <= 4'd2; t[2] <= 4'd3; t[3] <= 4'd4;
    end
  end

  // async read of the row-major flat address i*2 + j
  assign z = t[{i, j}];

endmodule
