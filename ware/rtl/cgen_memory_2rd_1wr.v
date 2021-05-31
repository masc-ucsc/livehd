
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

module cgen_memory_2rd_1wr
  #(parameter BITS = 4, SIZE=128, FWD=1, LATENCY_0=1, LATENCY_1=1, WENSIZE=2)
    (input clock

      // RD PORT 0
     ,input [`log2(SIZE)-1:0]  rd_addr_0
     ,input                    rd_enable_0
     ,output reg [BITS-1:0]    rd_dout_0

      // RD PORT 1
     ,input [`log2(SIZE)-1:0]  rd_addr_1
     ,input                    rd_enable_1
     ,output reg [BITS-1:0]    rd_dout_1

     // WR PORT 0
     ,input [`log2(SIZE)-1:0]  wr_addr_0
     ,input [WENSIZE-1:0]      wr_enable_0
     ,input [BITS-1:0]         wr_din_0
     );

generate
  if (WENSIZE==1) begin
    reg [BITS-1:0]        data[SIZE-1:0]; // synthesis syn_ramstyle = "block_ram"
  end else begin
    reg [(BITS/WENSIZE)-1:0] data[SIZE-1:0][WENSIZE-1:0]; // synthesis syn_ramstyle = "block_ram"
  end
endgenerate

// WRITE
generate
  if (WENSIZE==1) begin
    always @(posedge clock) begin
      if (wr_enable_0) begin
        data[wr_addr_0] <= wr_din_0;
      end
    end
  end else begin: DATA_WR_BLOCK
    genvar i;

    for(i=0;i<WENSIZE;i=i+1) begin
      always @(posedge clock) begin
        if (wr_enable_0[i]) begin
          data[wr_addr_0][i] <= wr_din_0[(BITS-i*BITS/WENSIZE-1):(BITS-(i+1)*BITS/WENSIZE)];
        end
      end
    end
  end
endgenerate

// READ 0
  reg [BITS-1:0]        d0_mem;
generate
  if (WENSIZE==1) begin
    always_comb begin
      if (rd_addr_0 < SIZE && rd_enable_0)
        d0_mem = data[rd_addr_0];
      else
        d0_mem = 'bx;
    end
  end else begin
    genvar i;

    for(i=0;i<WENSIZE;i=i+1) begin
      always_comb begin
        if (rd_addr_0 < SIZE && rd_enable_0) begin
          d0_mem[(BITS-i*BITS/WENSIZE-1):(BITS-(i+1)*BITS/WENSIZE)] = data[rd_addr_0][i];
        end else begin
          d0_mem[(BITS-i*BITS/WENSIZE-1):(BITS-(i+1)*BITS/WENSIZE)] = 'bx;
        end
      end
    end
  end
endgenerate

  reg [BITS-1:0]        d0_fwd;

generate
  if (FWD) begin:BLOCK_FWD_TRUE
    integer i;
    // If WENSIZE==4 and SIZE=64
    //   {wr_enable_0[3] && wr_hit? din[63:48] : d0_mem[63:48]
    //   ,wr_enable_0[2] && wr_hit? din[47:32] : d0_mem[47:32]
    //   ,wr_enable_0[1] && wr_hit? din[31:16] : d0_mem[31:16]
    //   ,wr_enable_0[0] && wr_hit? din[15:0 ] : d0_mem[15:0 ]
    //   }
    reg                   fwd_decision_cmp_0;

    always_comb begin
      fwd_decision_cmp_0 = wr_addr_0 == rd_addr_0;
      for(i=0;i<WENSIZE;i=i+1) begin: FWD_BLOCK_CALC_0
        d0_fwd[(BITS-i*BITS/WENSIZE-1):(BITS-(i+1)*BITS/WENSIZE)] =
          wr_enable_0[i] && fwd_decision_cmp_0?
          wr_din_0[(BITS-i*BITS/WENSIZE-1):(BITS-(i+1)*BITS/WENSIZE)]:
          d0_mem  [(BITS-i*BITS/WENSIZE-1):(BITS-(i+1)*BITS/WENSIZE)];
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
    always @(posedge clock) begin
      rd_dout_0 <= d0_fwd;
    end
  end else begin:BLOCK2
    assign rd_dout_0 = d0_fwd;
  end
endgenerate


endmodule

