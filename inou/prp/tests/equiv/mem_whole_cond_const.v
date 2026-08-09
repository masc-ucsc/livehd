module \mem_whole_cond_const.condconst (
  input               clr,
  input               fill,
  input               we,
  input  signed [2:0] waddr,
  input  signed [9:0] wdata,
  input  signed [2:0] idx,
  output        [9:0] r,
  input               clock
);
  reg [9:0] arr[7:0];
  integer i;
  wire [2:0] uwaddr = waddr;
  wire [2:0] uidx   = idx;
  always @(posedge clock) begin
    if (clr)       for (i=0;i<8;i=i+1) arr[i] <= 10'h0;
    else if (fill) for (i=0;i<8;i=i+1) arr[i] <= 10'h3ff;
    else if (we)   arr[uwaddr] <= wdata;
  end
  assign r = arr[uidx];
endmodule
