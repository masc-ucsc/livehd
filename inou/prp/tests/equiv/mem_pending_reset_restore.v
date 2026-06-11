module \mem_pending_reset_restore.restore (
  input            clock,
  input            reset,
  input      [3:0] a,
  input            i,
  input            we,
  output     [3:0] z
);

  reg [3:0] data[1:0];

  initial begin
    data[0] = 4'd4;
    data[1] = 4'd9;
  end

  always @(posedge clock) begin
    if (reset) begin
      data[0] <= 4'd4;
      data[1] <= 4'd9;
    end else if (we) begin
      data[i] <= a;
    end
  end

  assign z = (!reset && we) ? a : data[i];

endmodule
