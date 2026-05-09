module memory_rw(
  input        clk,
  input        we,
  input  [1:0] waddr,
  input  [1:0] raddr,
  input  [7:0] din,
  output [7:0] dout
);
reg [7:0] mem [0:3];

always @(posedge clk) begin
  if (we) begin
    mem[waddr] <= din;
  end
end

assign dout = (we && (waddr == raddr)) ? din : mem[raddr];
endmodule
