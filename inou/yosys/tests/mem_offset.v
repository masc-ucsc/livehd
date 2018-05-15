module mem_offset
   (
    input 		                  clk,
    input [32-1:0]      raddr,
    input [32-1:0]      waddr,
    input 		                  we,
    input      [32-1:0] din,
    output reg [32-1:0] dout
    );

   reg [32-1:0]     mem[(1<<32)-1:0];

  always @(posedge clk) begin
    mem[waddr] <= din;
    if (we)
      dout <= mem[raddr];
  end

endmodule
