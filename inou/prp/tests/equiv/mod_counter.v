module \mod_counter.counter (
  input        clock,
  input        reset,
  input        enable,
  output [7:0] count
);

  reg [7:0] count_r;
  assign count = count_r;

  always @(posedge clock) begin
    if (reset) count_r <= 8'd0;
    else if (enable && count_r < 8'd200) count_r <= count_r + 8'd1;
  end

endmodule
