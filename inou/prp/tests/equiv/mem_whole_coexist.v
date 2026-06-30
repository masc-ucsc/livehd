module \mem_whole_coexist.coexist (
  input  signed [79:0] inp,
  input  signed  [2:0] waddr,
  input  signed  [9:0] wdata,
  input                we,
  input  signed  [2:0] idx,
  output        [9:0]  r,
  output       [79:0]  allout,
  input                clock
);
  reg [9:0] data[7:0];
  integer i;
  wire [2:0] uwaddr = waddr;
  wire [2:0] uidx   = idx;
  always @(posedge clock) begin
    for (i=0;i<8;i=i+1) data[i] <= inp[i*10 +: 10];  // bulk update
    if (we) data[uwaddr] <= wdata;                    // per-port OVERRIDES
  end
  assign r      = data[uidx];
  assign allout = {data[7],data[6],data[5],data[4],data[3],data[2],data[1],data[0]};
endmodule
