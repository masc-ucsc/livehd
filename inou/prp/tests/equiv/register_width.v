module top (
  input            clock,
  input      [4:0] index,
  output     [4:0] value
);

  // The generic picks the NARROW width, so the register is 4 bits and the
  // 5-bit index truncates into it (`wrap`). A dropped declared width would
  // make this 5 bits and truncate nothing.
  reg [3:0] decoded;

  always @(posedge clock) begin
    decoded <= index[3:0];
  end

  assign value = {1'b0, decoded};

endmodule
