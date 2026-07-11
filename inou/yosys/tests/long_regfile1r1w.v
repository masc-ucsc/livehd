
module regfile1r1w
    (input                    clk
     ,input [2-1:0]           waddr0
     ,input                   we0
     ,input [4-1:0]          din0
     ,input [2-1:0]           raddr0

     ,output reg [4-1:0]     q0

     );

   reg [4-1:0]                      rf[4-1:0]; // synthesis syn_ramstyle = "block_ram"

   reg [4-1:0] q0_next;
   always @(*) begin
     if (we0)
       q0_next = din0;
     else
       q0_next = rf[raddr0];
   end

   always @(posedge clk) begin
     q0 <= q0_next;
   end

   always @(posedge clk) begin
     if (we0) begin
       rf[waddr0] <= din0;
     end
   end

endmodule

