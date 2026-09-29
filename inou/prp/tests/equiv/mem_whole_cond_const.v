module \mem_whole_cond_const.condconst (
  input               clr,
  input               fill,
  input               we,
  input  signed [1:0] waddr,
  input  signed [5:0] wdata,
  input  signed [1:0] idx,
  output        [5:0] r,
  input               clock
);
  reg [5:0] arr[3:0];
  integer i;
  wire [1:0] uwaddr = waddr;
  wire [1:0] uidx   = idx;
  always @(posedge clock) begin
    if (clr)       for (i=0;i<4;i=i+1) arr[i] <= 6'h0;
    else if (fill) for (i=0;i<4;i=i+1) arr[i] <= 6'h3f;
    else if (we)   arr[uwaddr] <= wdata;
  end
  assign r = arr[uidx];
endmodule
