module \mclk_derived.derived (
  input            clock,
  input            reset,
  input            clk_b,
  input            gate,
  input      [7:0] da,
  input      [7:0] db,
  output     [7:0] qa,
  output     [7:0] qb
);

  reg [7:0] qa_r;
  reg [7:0] qb_r;

  // gclk is a DERIVED clock: an internal combinational signal, not a port.
  wire gclk;
  assign gclk = clk_b & gate;

  always @(posedge clock) if (reset) qa_r <= 8'd0; else qa_r <= da;
  always @(posedge gclk)  if (reset) qb_r <= 8'd0; else qb_r <= db;

  assign qa = qa_r;
  assign qb = qb_r;

endmodule
