`define log2(n)   ((n) <= (1<<0) ? 0 : (n) <= (1<<1) ? 1 :\
                   (n) <= (1<<2) ? 2 : (n) <= (1<<3) ? 3 :\
                   (n) <= (1<<4) ? 4 : (n) <= (1<<5) ? 5 :\
                   (n) <= (1<<6) ? 6 : (n) <= (1<<7) ? 7 :\
                   (n) <= (1<<8) ? 8 : (n) <= (1<<9) ? 9 :\
                   (n) <= (1<<10) ? 10 : (n) <= (1<<11) ? 11 :\
                   (n) <= (1<<12) ? 12 : (n) <= (1<<13) ? 13 :\
                   (n) <= (1<<14) ? 14 : (n) <= (1<<15) ? 15 :\
                   (n) <= (1<<16) ? 16 : (n) <= (1<<17) ? 17 :\
                   (n) <= (1<<18) ? 18 : (n) <= (1<<19) ? 19 :\
                   (n) <= (1<<20) ? 20 : (n) <= (1<<21) ? 21 :\
                   (n) <= (1<<22) ? 22 : (n) <= (1<<23) ? 23 :\
                   (n) <= (1<<24) ? 24 : (n) <= (1<<25) ? 25 :\
                   (n) <= (1<<26) ? 26 : (n) <= (1<<27) ? 27 :\
                   (n) <= (1<<28) ? 28 : (n) <= (1<<29) ? 29 :\
                   (n) <= (1<<30) ? 30 : (n) <= (1<<31) ? 31 : 32)

module cgen_memory_multiclock_1rd_18wr
  #(parameter BITS = 4, SIZE=128, FWD=1, LATENCY_0=1, WENSIZE=1)
    (
      // RD PORT 0
      input [`log2(SIZE)-1:0]  rd_addr_0
     ,input                    rd_enable_0
     ,input                    rd_clock_0
     ,output reg [BITS-1:0]    rd_dout_0

     // WR PORT 0
     ,input [`log2(SIZE)-1:0]  wr_addr_0
     ,input [WENSIZE-1:0]      wr_enable_0
     ,input                    wr_clock_0
     ,input [BITS-1:0]         wr_din_0

     // WR PORT 1
     ,input [`log2(SIZE)-1:0]  wr_addr_1
     ,input [WENSIZE-1:0]      wr_enable_1
     ,input                    wr_clock_1
     ,input [BITS-1:0]         wr_din_1

     // WR PORT 2
     ,input [`log2(SIZE)-1:0]  wr_addr_2
     ,input [WENSIZE-1:0]      wr_enable_2
     ,input                    wr_clock_2
     ,input [BITS-1:0]         wr_din_2

     // WR PORT 3
     ,input [`log2(SIZE)-1:0]  wr_addr_3
     ,input [WENSIZE-1:0]      wr_enable_3
     ,input                    wr_clock_3
     ,input [BITS-1:0]         wr_din_3

     // WR PORT 4
     ,input [`log2(SIZE)-1:0]  wr_addr_4
     ,input [WENSIZE-1:0]      wr_enable_4
     ,input                    wr_clock_4
     ,input [BITS-1:0]         wr_din_4

     // WR PORT 5
     ,input [`log2(SIZE)-1:0]  wr_addr_5
     ,input [WENSIZE-1:0]      wr_enable_5
     ,input                    wr_clock_5
     ,input [BITS-1:0]         wr_din_5

     // WR PORT 6
     ,input [`log2(SIZE)-1:0]  wr_addr_6
     ,input [WENSIZE-1:0]      wr_enable_6
     ,input                    wr_clock_6
     ,input [BITS-1:0]         wr_din_6

     // WR PORT 7
     ,input [`log2(SIZE)-1:0]  wr_addr_7
     ,input [WENSIZE-1:0]      wr_enable_7
     ,input                    wr_clock_7
     ,input [BITS-1:0]         wr_din_7

     // WR PORT 8
     ,input [`log2(SIZE)-1:0]  wr_addr_8
     ,input [WENSIZE-1:0]      wr_enable_8
     ,input                    wr_clock_8
     ,input [BITS-1:0]         wr_din_8

     // WR PORT 9
     ,input [`log2(SIZE)-1:0]  wr_addr_9
     ,input [WENSIZE-1:0]      wr_enable_9
     ,input                    wr_clock_9
     ,input [BITS-1:0]         wr_din_9

     // WR PORT 10
     ,input [`log2(SIZE)-1:0]  wr_addr_10
     ,input [WENSIZE-1:0]      wr_enable_10
     ,input                    wr_clock_10
     ,input [BITS-1:0]         wr_din_10

     // WR PORT 11
     ,input [`log2(SIZE)-1:0]  wr_addr_11
     ,input [WENSIZE-1:0]      wr_enable_11
     ,input                    wr_clock_11
     ,input [BITS-1:0]         wr_din_11

     // WR PORT 12
     ,input [`log2(SIZE)-1:0]  wr_addr_12
     ,input [WENSIZE-1:0]      wr_enable_12
     ,input                    wr_clock_12
     ,input [BITS-1:0]         wr_din_12

     // WR PORT 13
     ,input [`log2(SIZE)-1:0]  wr_addr_13
     ,input [WENSIZE-1:0]      wr_enable_13
     ,input                    wr_clock_13
     ,input [BITS-1:0]         wr_din_13

     // WR PORT 14
     ,input [`log2(SIZE)-1:0]  wr_addr_14
     ,input [WENSIZE-1:0]      wr_enable_14
     ,input                    wr_clock_14
     ,input [BITS-1:0]         wr_din_14

     // WR PORT 15
     ,input [`log2(SIZE)-1:0]  wr_addr_15
     ,input [WENSIZE-1:0]      wr_enable_15
     ,input                    wr_clock_15
     ,input [BITS-1:0]         wr_din_15

     // WR PORT 16
     ,input [`log2(SIZE)-1:0]  wr_addr_16
     ,input [WENSIZE-1:0]      wr_enable_16
     ,input                    wr_clock_16
     ,input [BITS-1:0]         wr_din_16

     // WR PORT 17
     ,input [`log2(SIZE)-1:0]  wr_addr_17
     ,input [WENSIZE-1:0]      wr_enable_17
     ,input                    wr_clock_17
     ,input [BITS-1:0]         wr_din_17
     );

localparam MASKSIZE = BITS/WENSIZE;

reg [BITS-1:0] d0_mem;

generate
    (*ram_style = "block" *) reg [BITS-1:0] data[SIZE-1:0]; // synthesis syn_ramstyle = "block_ram"

    integer i;

    always @(posedge wr_clock_0) begin
      for(i=0;i<WENSIZE;i=i+1) if(wr_enable_0[i]) data[wr_addr_0][i*MASKSIZE +: MASKSIZE] <= wr_din_0[i*MASKSIZE +: MASKSIZE];
    end
    always @(posedge wr_clock_1) begin
      for(i=0;i<WENSIZE;i=i+1) if(wr_enable_1[i]) data[wr_addr_1][i*MASKSIZE +: MASKSIZE] <= wr_din_1[i*MASKSIZE +: MASKSIZE];
    end
    always @(posedge wr_clock_2) begin
      for(i=0;i<WENSIZE;i=i+1) if(wr_enable_2[i]) data[wr_addr_2][i*MASKSIZE +: MASKSIZE] <= wr_din_2[i*MASKSIZE +: MASKSIZE];
    end
    always @(posedge wr_clock_3) begin
      for(i=0;i<WENSIZE;i=i+1) if(wr_enable_3[i]) data[wr_addr_3][i*MASKSIZE +: MASKSIZE] <= wr_din_3[i*MASKSIZE +: MASKSIZE];
    end
    always @(posedge wr_clock_4) begin
      for(i=0;i<WENSIZE;i=i+1) if(wr_enable_4[i]) data[wr_addr_4][i*MASKSIZE +: MASKSIZE] <= wr_din_4[i*MASKSIZE +: MASKSIZE];
    end
    always @(posedge wr_clock_5) begin
      for(i=0;i<WENSIZE;i=i+1) if(wr_enable_5[i]) data[wr_addr_5][i*MASKSIZE +: MASKSIZE] <= wr_din_5[i*MASKSIZE +: MASKSIZE];
    end
    always @(posedge wr_clock_6) begin
      for(i=0;i<WENSIZE;i=i+1) if(wr_enable_6[i]) data[wr_addr_6][i*MASKSIZE +: MASKSIZE] <= wr_din_6[i*MASKSIZE +: MASKSIZE];
    end
    always @(posedge wr_clock_7) begin
      for(i=0;i<WENSIZE;i=i+1) if(wr_enable_7[i]) data[wr_addr_7][i*MASKSIZE +: MASKSIZE] <= wr_din_7[i*MASKSIZE +: MASKSIZE];
    end
    always @(posedge wr_clock_8) begin
      for(i=0;i<WENSIZE;i=i+1) if(wr_enable_8[i]) data[wr_addr_8][i*MASKSIZE +: MASKSIZE] <= wr_din_8[i*MASKSIZE +: MASKSIZE];
    end
    always @(posedge wr_clock_9) begin
      for(i=0;i<WENSIZE;i=i+1) if(wr_enable_9[i]) data[wr_addr_9][i*MASKSIZE +: MASKSIZE] <= wr_din_9[i*MASKSIZE +: MASKSIZE];
    end
    always @(posedge wr_clock_10) begin
      for(i=0;i<WENSIZE;i=i+1) if(wr_enable_10[i]) data[wr_addr_10][i*MASKSIZE +: MASKSIZE] <= wr_din_10[i*MASKSIZE +: MASKSIZE];
    end
    always @(posedge wr_clock_11) begin
      for(i=0;i<WENSIZE;i=i+1) if(wr_enable_11[i]) data[wr_addr_11][i*MASKSIZE +: MASKSIZE] <= wr_din_11[i*MASKSIZE +: MASKSIZE];
    end
    always @(posedge wr_clock_12) begin
      for(i=0;i<WENSIZE;i=i+1) if(wr_enable_12[i]) data[wr_addr_12][i*MASKSIZE +: MASKSIZE] <= wr_din_12[i*MASKSIZE +: MASKSIZE];
    end
    always @(posedge wr_clock_13) begin
      for(i=0;i<WENSIZE;i=i+1) if(wr_enable_13[i]) data[wr_addr_13][i*MASKSIZE +: MASKSIZE] <= wr_din_13[i*MASKSIZE +: MASKSIZE];
    end
    always @(posedge wr_clock_14) begin
      for(i=0;i<WENSIZE;i=i+1) if(wr_enable_14[i]) data[wr_addr_14][i*MASKSIZE +: MASKSIZE] <= wr_din_14[i*MASKSIZE +: MASKSIZE];
    end
    always @(posedge wr_clock_15) begin
      for(i=0;i<WENSIZE;i=i+1) if(wr_enable_15[i]) data[wr_addr_15][i*MASKSIZE +: MASKSIZE] <= wr_din_15[i*MASKSIZE +: MASKSIZE];
    end
    always @(posedge wr_clock_16) begin
      for(i=0;i<WENSIZE;i=i+1) if(wr_enable_16[i]) data[wr_addr_16][i*MASKSIZE +: MASKSIZE] <= wr_din_16[i*MASKSIZE +: MASKSIZE];
    end
    always @(posedge wr_clock_17) begin
      for(i=0;i<WENSIZE;i=i+1) if(wr_enable_17[i]) data[wr_addr_17][i*MASKSIZE +: MASKSIZE] <= wr_din_17[i*MASKSIZE +: MASKSIZE];
    end

    if (LATENCY_0==1) begin:BLOCK_SYNC_RD
      always @(posedge rd_clock_0) begin
        if (rd_enable_0)
          d0_mem <= data[rd_addr_0];
        else
          d0_mem <= {BITS{1'bx}};
      end
    end else begin:BLOCK_ASYNC_RD
      always_comb begin
        if (rd_enable_0)
          d0_mem = data[rd_addr_0];
        else
          d0_mem = {BITS{1'bx}};
      end
    end
endgenerate

reg [BITS-1:0] d0_fwd;

generate
  if (FWD) begin:BLOCK_FWD_TRUE
    genvar j;
    for(j=0;j<WENSIZE;j=j+1) begin:FWD_BLOCK_CALC_0
      always_comb begin
        d0_fwd[j*MASKSIZE +: MASKSIZE] = d0_mem[j*MASKSIZE +: MASKSIZE];
        if (wr_enable_17[j] && (wr_addr_17 == rd_addr_0)) d0_fwd[j*MASKSIZE +: MASKSIZE] = wr_din_17[j*MASKSIZE +: MASKSIZE];
        if (wr_enable_16[j] && (wr_addr_16 == rd_addr_0)) d0_fwd[j*MASKSIZE +: MASKSIZE] = wr_din_16[j*MASKSIZE +: MASKSIZE];
        if (wr_enable_15[j] && (wr_addr_15 == rd_addr_0)) d0_fwd[j*MASKSIZE +: MASKSIZE] = wr_din_15[j*MASKSIZE +: MASKSIZE];
        if (wr_enable_14[j] && (wr_addr_14 == rd_addr_0)) d0_fwd[j*MASKSIZE +: MASKSIZE] = wr_din_14[j*MASKSIZE +: MASKSIZE];
        if (wr_enable_13[j] && (wr_addr_13 == rd_addr_0)) d0_fwd[j*MASKSIZE +: MASKSIZE] = wr_din_13[j*MASKSIZE +: MASKSIZE];
        if (wr_enable_12[j] && (wr_addr_12 == rd_addr_0)) d0_fwd[j*MASKSIZE +: MASKSIZE] = wr_din_12[j*MASKSIZE +: MASKSIZE];
        if (wr_enable_11[j] && (wr_addr_11 == rd_addr_0)) d0_fwd[j*MASKSIZE +: MASKSIZE] = wr_din_11[j*MASKSIZE +: MASKSIZE];
        if (wr_enable_10[j] && (wr_addr_10 == rd_addr_0)) d0_fwd[j*MASKSIZE +: MASKSIZE] = wr_din_10[j*MASKSIZE +: MASKSIZE];
        if (wr_enable_9[j] && (wr_addr_9 == rd_addr_0)) d0_fwd[j*MASKSIZE +: MASKSIZE] = wr_din_9[j*MASKSIZE +: MASKSIZE];
        if (wr_enable_8[j] && (wr_addr_8 == rd_addr_0)) d0_fwd[j*MASKSIZE +: MASKSIZE] = wr_din_8[j*MASKSIZE +: MASKSIZE];
        if (wr_enable_7[j] && (wr_addr_7 == rd_addr_0)) d0_fwd[j*MASKSIZE +: MASKSIZE] = wr_din_7[j*MASKSIZE +: MASKSIZE];
        if (wr_enable_6[j] && (wr_addr_6 == rd_addr_0)) d0_fwd[j*MASKSIZE +: MASKSIZE] = wr_din_6[j*MASKSIZE +: MASKSIZE];
        if (wr_enable_5[j] && (wr_addr_5 == rd_addr_0)) d0_fwd[j*MASKSIZE +: MASKSIZE] = wr_din_5[j*MASKSIZE +: MASKSIZE];
        if (wr_enable_4[j] && (wr_addr_4 == rd_addr_0)) d0_fwd[j*MASKSIZE +: MASKSIZE] = wr_din_4[j*MASKSIZE +: MASKSIZE];
        if (wr_enable_3[j] && (wr_addr_3 == rd_addr_0)) d0_fwd[j*MASKSIZE +: MASKSIZE] = wr_din_3[j*MASKSIZE +: MASKSIZE];
        if (wr_enable_2[j] && (wr_addr_2 == rd_addr_0)) d0_fwd[j*MASKSIZE +: MASKSIZE] = wr_din_2[j*MASKSIZE +: MASKSIZE];
        if (wr_enable_1[j] && (wr_addr_1 == rd_addr_0)) d0_fwd[j*MASKSIZE +: MASKSIZE] = wr_din_1[j*MASKSIZE +: MASKSIZE];
        if (wr_enable_0[j] && (wr_addr_0 == rd_addr_0)) d0_fwd[j*MASKSIZE +: MASKSIZE] = wr_din_0[j*MASKSIZE +: MASKSIZE];
      end
    end
  end else begin:BLOCK_FWD_FALSE
    always_comb begin
      d0_fwd = d0_mem;
    end
  end
endgenerate

generate
  if (LATENCY_0==1) begin:BLOCK1
    always @(posedge rd_clock_0) begin
      rd_dout_0 <= d0_fwd;
    end
  end else begin:BLOCK2
    assign rd_dout_0 = d0_fwd;
  end
endgenerate

endmodule
