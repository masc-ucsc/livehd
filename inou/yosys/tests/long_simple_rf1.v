
module long_simple_rf1
    (input                    clk
     ,input [7-1:0]           waddr0
     ,input                   we0
     ,input [13-1:0]          din0
     ,input [7-1:0]           raddr0

     ,output [13-1:0]         q0

     );

   reg [13-1:0]                      rf[16-1:0]; // synthesis syn_ramstyle = "block_ram"

   reg [13-1:0] q0_next;
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

