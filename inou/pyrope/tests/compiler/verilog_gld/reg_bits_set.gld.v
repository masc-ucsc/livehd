module reg_bits_set (
  input        clock,
  input        reset,
  input  [7:0] \input ,
  output [7:0] out
);

  wire signed [8:0] a;
  assign a = {1'b0, \input };

  wire signed [7:0] ff_next = ff == 0 ? a  : ff - 1;
  reg signed [7:0] ff;
  always @(posedge clock) begin
    if (reset) begin
      ff <= 'd0;
    end else begin
      ff <= ff_next;
    end
  end

  assign out = ff;

endmodule
