module blocking_array_slices(input logic clk, en, addr, input logic [3:0] din, input logic ra, output reg [3:0] q);
  reg [3:0] mem[2];
  always @(posedge clk) begin
    if (en) begin
      mem[addr][1:0] = din[1:0];
      mem[addr][3:2] = din[3:2];
    end
    q <= mem[ra];
  end
endmodule
