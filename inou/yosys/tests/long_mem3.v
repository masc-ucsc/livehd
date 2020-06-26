
module mem3
    (input                clk
     ,input [2:0]         waddr0
     ,input               we0
     ,input         din0
     ,input [2:0]         raddr1
     ,output reg    q1
     ,input [2:0]         raddr2
     ,output reg    q2
     );

   reg rf[9:0]; // a bit bigger than needed to test strange cases

   reg none;

   always @(posedge clk) begin
     q1 <= rf[raddr1];
     none <= rf[raddr2];
     if (we0) begin
       rf[waddr0] <= din0;
     end
   end

endmodule


