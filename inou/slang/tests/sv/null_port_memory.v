// :test: roundtrip
module null_port_bank(clk, we, addr, d, q,);
  input clk, we;
  input [1:0] addr;
  input [7:0] d;
  output reg [7:0] q;
  reg [7:0] mem [0:3];
  always @(posedge clk) begin
    if (we) mem[addr] <= d;
    q <= mem[addr];
  end
endmodule
module null_port_memory(input clk, we, input [1:0] addr, input [7:0] d, output [7:0] q);
  null_port_bank bank(.clk(clk), .we(we), .addr(addr), .d(d), .q(q));
endmodule
