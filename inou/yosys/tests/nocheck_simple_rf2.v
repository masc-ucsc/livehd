
module simple_rf2
    (input                    clk
     ,input [3-1:0]           waddr0
     ,input [1:0]          din0
     ,input [3-1:0]           raddr0

     ,output [1:0]         q0

     );

   reg [1:0]                rf[8-1:0]; // synthesis syn_ramstyle = "block_ram"

   always @(*) begin
     q0 = rf[raddr0];
   end

   always @(posedge clk) begin
     rf[waddr0] <= din0;
   end

endmodule

