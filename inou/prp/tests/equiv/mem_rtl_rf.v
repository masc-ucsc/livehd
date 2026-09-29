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

  reg [3:0] res[15:0];

  always @(posedge clock) begin
    if (we0) res[wraddr] <= din0;
  end

  // ordering="old": async reads of the committed state only (DEFINED, so not
  // ordering="none", which leaves the read-during-write window undefined)
  assign q0 = res[raddr0];
  assign q1 = res[raddr1];

endmodule
