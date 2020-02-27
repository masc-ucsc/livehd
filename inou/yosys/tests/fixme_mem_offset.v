module mem_offset
   (
    input 		                  clk,
    input [2-1:0]      raddr,
    input [2-1:0]      waddr,
    input 		                  we,
    input      [2-1:0] din,
    output reg [2-1:0] dout
    );

   reg [2-1:0] mem [5:50];

  always @(posedge clk) begin
    mem[waddr] <= din;
    if (we)
      dout <= mem[raddr];
  end

endmodule
