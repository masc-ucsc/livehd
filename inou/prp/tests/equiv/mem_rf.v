module \mem_rf.rf (
  input            clock,
  input      [2:0] waddr,
  input      [3:0] din,
  input            we,
  input      [2:0] ra0,
  input      [2:0] ra1,
  output     [3:0] q0,
  output     [3:0] q1
);

  reg [3:0] data[7:0];

  always @(posedge clock) begin
    if (we) data[waddr] <= din;
  end

  assign q0 = (we && (waddr == ra0)) ? din : data[ra0];
  assign q1 = (we && (waddr == ra1)) ? din : data[ra1];

endmodule
