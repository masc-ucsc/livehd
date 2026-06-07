module \mod_mux_aligned.pick (
  input            clock,
  input            sel,
  input      [7:0] a,
  input      [7:0] b,
  output     [8:0] out
);

  reg       sd;
  reg [7:0] ad;
  reg [7:0] bd;

  always @(posedge clock) begin
    sd <= sel;
    ad <= a;
    bd <= b;
  end

  assign out = sd ? (ad + 9'd1) : (bd + 9'd2);

endmodule
