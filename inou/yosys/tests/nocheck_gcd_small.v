module gcd_small(input clk,
           input reset,
           input [14:0] a,
           input [14:0] b,
           input start,
           output reg [14:0] res,
           output reg done);

  reg [14:0] x;
  reg [14:0] x_next;

  reg [14:0] y;
  reg [14:0] y_next;

  reg pending;
  reg pending_next;

  reg done;
  reg done_next;

  always_comb begin
    if (start) begin
      x_next = a;
      y_next = b;
      pending_next = 1;
    end else begin
      pending_next = pending;
      if (x > y) begin
        x_next = x - y;
        y_next = y;
      end else if (x < y) begin
        y_next = y - x;
        x_next = x;
      end else begin
        y_next = 0;
        x_next = x;
      end
    end

    if (y == 0) begin
      done_next = pending;
      res       = x;
    end else begin
      done_next = 0;
      res       = x;
    end
  end

  always @(posedge clk) begin
    if (reset) begin
      done <= 0;
      x    <= 0;
      y    <= 0;
      pending <= 0;
    end else begin
      done <= done_next;
      x    <= x_next;
      y    <= y_next;
      pending <= pending_next;
    end
  end

endmodule


