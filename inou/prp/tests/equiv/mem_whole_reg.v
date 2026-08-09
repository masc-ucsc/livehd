module \mem_whole_reg.regarr (
  input  signed [79:0] inp,
  input  signed  [2:0] idx,
  output        [9:0]  r,
  output       [79:0]  allout,
  input                clock
);
  reg [9:0] arr[7:0];
  integer i;
  always @(posedge clock) for (i=0;i<8;i=i+1) arr[i] <= inp[i*10 +: 10];
  wire [2:0] uidx = idx;
  assign r      = arr[uidx];
  assign allout = {arr[7],arr[6],arr[5],arr[4],arr[3],arr[2],arr[1],arr[0]};
endmodule
