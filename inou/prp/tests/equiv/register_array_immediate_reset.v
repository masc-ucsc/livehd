// Independent specification: each byte resets immediately and holds unless
// selected for writing. A one-cycle reset pulse must clear the entire bank.
module top(
    input wire clock, reset, we,
    input wire [1:0] waddr,
    input wire [7:0] data,
    input wire [1:0] raddr,
    output wire [7:0] q,
    output wire [31:0] all_data
);
  reg [31:0] bank;
  always @(posedge clock or posedge reset) begin
    if (reset)
      bank <= 32'b0;
    else if (we)
      bank[waddr * 8 +: 8] <= data;
  end
  assign q = bank[raddr * 8 +: 8];
  assign all_data = bank;
endmodule
