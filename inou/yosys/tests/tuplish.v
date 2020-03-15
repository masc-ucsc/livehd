
module tuplish
  (input                clk
    ,input [2:0]         waddr0
    ,input               we0
    ,input         [3:0] din0
    ,input [2:0]         raddr1
    ,output reg    [3:0] q1
    ,input [2:0]         raddr2
    ,output reg    [3:0] q2
  );

  reg [3:0] tup[7:0];

  integer i;
  always_comb begin
    tup[0] = 4'h0;
    tup[1] = 4'h0;
    tup[2] = 4'h0;
    tup[3] = 4'h0;
    tup[4] = 4'h0;
    tup[5] = 4'h0;
    tup[6] = 4'h0;
    tup[7] = 4'h0;
    if (we0) begin
      tup[waddr0] = din0;
    end
    q1 = tup[raddr1];
    q2 = tup[raddr2];
  end

endmodule

