module \reg_delay_init.delay (
  input            clock,
  input            reset,
  input      [7:0] a,
  output reg [8:0] o
);

  always @(posedge clock) begin
    if (reset) o <= 9'd7;
    else       o <= a;
  end

endmodule
