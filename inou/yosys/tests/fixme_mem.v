
module mem
    (input                clk
     ,input [3:0]         waddr0
     ,input               we0
     ,input [1:0]        din0
     ,input [3:0]         raddr1
     ,output reg [1:0]   q1
     );

   reg [1:0] rf[15:0];

   always @(posedge clk) begin
     q1 <= rf[raddr1];
     if (we0) begin
       rf[waddr0] <= din0;
     end
   end

endmodule


