module \mod_call_mod.top (
  input            clock,
  input      [7:0] a,
  output reg [7:0] out
);

  reg [7:0] d0;

  always @(posedge clock) begin
    d0  <= a;
    out <= d0;
  end

endmodule
