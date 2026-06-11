module \mod_twopaths.twopaths (
  input             clock,
  input      [31:0] a,
  input             opt,
  output     [31:0] o,
  output     [31:0] o2
);

  reg [31:0] tmp1;
  reg [31:0] tmp2;

  always @(posedge clock) begin
    tmp1 <= a;
    tmp2 <= tmp1;
  end

  assign o  = opt ? tmp1 : tmp2;
  assign o2 = o;

endmodule
