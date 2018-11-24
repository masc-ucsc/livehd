

module mem3
    (input                clk
     ,input [7:0]         waddr0
     ,input               we0
     ,input [63:0]        din0
     ,input [7:0]         raddr1
     ,output reg [63:0]   q1
     ,input [7:0]         raddr2
     ,output reg [63:0]   q2
     );

   reg [63:0] rf[255:0];

   reg [63:0] none;

   always @(posedge clk) begin
     q1 <= rf[raddr1];
     none <= rf[raddr2];
     if (we0) begin
       rf[waddr0] <= din0;
     end
   end

endmodule


