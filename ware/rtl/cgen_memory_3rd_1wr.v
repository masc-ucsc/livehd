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

module cgen_memory_3rd_1wr
  #(parameter BITS = 4, SIZE=128, FWD=1, LATENCY_0=1, WENSIZE=1)
    (input clock

     ,input [`log2(SIZE)-1:0]  rd_addr_0
     ,input                    rd_enable_0
     ,output reg [BITS-1:0]    rd_dout_0

     ,input [`log2(SIZE)-1:0]  rd_addr_1
     ,input                    rd_enable_1
     ,output reg [BITS-1:0]    rd_dout_1

     ,input [`log2(SIZE)-1:0]  rd_addr_2
     ,input                    rd_enable_2
     ,output reg [BITS-1:0]    rd_dout_2

     ,input [`log2(SIZE)-1:0]  wr_addr_0
     ,input [WENSIZE-1:0]      wr_enable_0
     ,input [BITS-1:0]         wr_din_0

);

localparam MASKSIZE = BITS/WENSIZE;

reg [BITS-1:0]        d0_mem;

reg [BITS-1:0]        d1_mem;

reg [BITS-1:0]        d2_mem;

generate
    reg [BITS-1:0]        data[SIZE-1:0];
    integer i;
    always @(posedge clock) begin
      for(i=0;i<WENSIZE;i=i+1) begin
        if(wr_enable_0[i]) begin
            data[wr_addr_0][i*MASKSIZE +: MASKSIZE] <=
              wr_din_0[i*MASKSIZE +: MASKSIZE];
        end
      end
    end

    always @(posedge clock) begin
      if (rd_enable_0)
        d0_mem <= data[rd_addr_0];
      else
        d0_mem <= {BITS{1'bx}};
      if (rd_enable_1)
        d1_mem <= data[rd_addr_1];
      else
        d1_mem <= {BITS{1'bx}};
      if (rd_enable_2)
        d2_mem <= data[rd_addr_2];
      else
        d2_mem <= {BITS{1'bx}};
    end
endgenerate

reg [BITS-1:0]        d0_fwd;

reg [BITS-1:0]        d1_fwd;

reg [BITS-1:0]        d2_fwd;

generate
  if (FWD) begin:BLOCK_FWD_TRUE
    reg [WENSIZE-1:0] fwd_decision_cmp_0rd_0wr;
    reg [WENSIZE-1:0] fwd_decision_cmp_1rd_0wr;
    reg [WENSIZE-1:0] fwd_decision_cmp_2rd_0wr;
    genvar j;
    for(j=0;j<WENSIZE;j=j+1) begin:FWD_BLOCK_CALC_0
    always_comb begin
      fwd_decision_cmp_0rd_0wr[j] = rd_addr_0 == wr_addr_0;
      fwd_decision_cmp_1rd_0wr[j] = rd_addr_1 == wr_addr_0;
      fwd_decision_cmp_2rd_0wr[j] = rd_addr_2 == wr_addr_0;
      d0_fwd[j*MASKSIZE +: MASKSIZE] = 
      wr_enable_0[j] && fwd_decision_cmp_0rd_0wr[j]?
      wr_din_0[j*MASKSIZE +: MASKSIZE]:
      d0_mem[j*MASKSIZE +: MASKSIZE];

      d1_fwd[j*MASKSIZE +: MASKSIZE] = 
      wr_enable_0[j] && fwd_decision_cmp_1rd_0wr[j]?
      wr_din_0[j*MASKSIZE +: MASKSIZE]:
      d1_mem[j*MASKSIZE +: MASKSIZE];

      d2_fwd[j*MASKSIZE +: MASKSIZE] = 
      wr_enable_0[j] && fwd_decision_cmp_2rd_0wr[j]?
      wr_din_0[j*MASKSIZE +: MASKSIZE]:
      d2_mem[j*MASKSIZE +: MASKSIZE];

      end
    end
  end else begin:BLOCK_FWD_FALSE
    always_comb begin
      d0_fwd = d0_mem;
      d1_fwd = d1_mem;
      d2_fwd = d2_mem;
    end
  end
endgenerate

generate
	if (LATENCY_0==1) begin:BLOCK1
    always @(posedge clock) begin
      rd_dout_0 <= d0_fwd;
      rd_dout_1 <= d1_fwd;
      rd_dout_2 <= d2_fwd;
    end
  end else begin:BLOCK2
    assign rd_dout_0 = d0_fwd;
    assign rd_dout_1 = d1_fwd;
    assign rd_dout_2 = d2_fwd;
  end
endgenerate
endmodule
