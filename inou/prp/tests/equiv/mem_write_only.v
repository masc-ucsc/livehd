module \mem_write_only.wro (
  input            clock,
  input      [7:0] a,
  input      [1:0] i,
  input      [7:0] p,
  output     [7:0] z
);

  // the write-only memory is unobservable from the ports; z is a passthrough
  reg [7:0] data[3:0];

  always @(posedge clock) begin
    data[i] <= a;
  end

  assign z = p;

endmodule
