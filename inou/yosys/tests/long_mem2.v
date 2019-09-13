

module mem2
    (input                clk
     ,input [1:0]         waddr0
     ,input               we0
     ,input [16:0]        din0
     ,input [1:0]         raddr1
     ,output reg [16:0]   q1
     ,input [1:0]         raddr2
     ,output reg [16:0]   q2
     );

   reg [16:0] rf[3:0];

   always @(posedge clk) begin
     q1 <= rf[raddr1];
     q2 <= rf[raddr2];
     if (we0) begin
       rf[waddr0] <= din0;
     end
   end

endmodule


