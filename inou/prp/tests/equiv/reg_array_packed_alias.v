module reg_array_packed_alias(
  input clock, reset, enable,
  input [1:0] addr,
  input [15:0] data,
  output [7:0] y
);
  reg [3:0][7:0] bank;
  always @(posedge clock or posedge reset)
    if (reset) bank <= 0;
    else if (enable) begin
      bank[1] <= data[7:0];
      bank[2] <= data[15:8];
    end
  // Independent packed expression: the untouched lanes retain committed data.
  wire [31:0] updated = enable ? {bank[3], data, bank[0]} : bank;
  assign y = updated >> {addr, 3'b000};
endmodule
