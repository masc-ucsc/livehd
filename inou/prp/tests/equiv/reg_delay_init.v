module \reg_delay_init.delay (
  input            clock,
  input            reset,
  input      [7:0] a,
  output     [8:0] o
);

  reg [8:0] r;

  always @(posedge clock) begin
    if (reset) r <= 9'd7;
    else       r <= a;
  end

  assign o = r;

endmodule
