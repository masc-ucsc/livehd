module \mem_rtl_rf.rtlmem (
  input            clock,
  input      [3:0] raddr0,
  input      [3:0] raddr1,
  input      [3:0] wraddr,
  input      [3:0] din0,
  input            we0,
  output     [3:0] q0,
  output     [3:0] q1
);

  reg [3:0] data[15:0];

  always @(posedge clock) begin
    if (we0) data[wraddr] <= din0;
  end

  // fwd=0: async reads of the committed state only
  assign q0 = data[raddr0];
  assign q1 = data[raddr1];

endmodule
