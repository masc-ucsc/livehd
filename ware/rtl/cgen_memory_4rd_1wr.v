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

module cgen_memory_4rd_1wr
  #(parameter BITS = 4, SIZE=128, FWD=1, LATENCY_0=1, WENSIZE=1,
    parameter INIT_EN=0, parameter [BITS*SIZE-1:0] INIT=0)
    (input clk

     // RD PORT 0
     ,input [`log2(SIZE)-1:0]  rd_addr_0
     ,input                    rd_enable_0
     ,output reg [BITS-1:0]    rd_dout_0

     // RD PORT 1
     ,input [`log2(SIZE)-1:0]  rd_addr_1
     ,input                    rd_enable_1
     ,output reg [BITS-1:0]    rd_dout_1

     // RD PORT 2
     ,input [`log2(SIZE)-1:0]  rd_addr_2
     ,input                    rd_enable_2
     ,output reg [BITS-1:0]    rd_dout_2

     // RD PORT 3
     ,input [`log2(SIZE)-1:0]  rd_addr_3
     ,input                    rd_enable_3
     ,output reg [BITS-1:0]    rd_dout_3

     // WR PORT 0
     ,input [`log2(SIZE)-1:0]  wr_addr_0
     ,input [WENSIZE-1:0]      wr_enable_0
     ,input [BITS-1:0]         wr_din_0
     );

localparam MASKSIZE = BITS/WENSIZE;

(*ram_style = "block" *) reg [BITS-1:0] data[SIZE-1:0]; // synthesis syn_ramstyle = "block_ram"

// Power-on contents (Memory cell `init` pin, entry 0 in the low BITS):
// yosys lifts this into $meminit.
generate
  if (INIT_EN) begin:BLOCK_INIT
    integer ii;
    initial begin
      for(ii=0;ii<SIZE;ii=ii+1) begin
        data[ii] = INIT[ii*BITS +: BITS];
      end
    end
  end
endgenerate

//WRITE (port-order statement priority: on a same-address collision the
//highest-numbered enabled port wins)
integer i;
always @(posedge clk) begin
  for(i=0;i<WENSIZE;i=i+1) begin
    if(wr_enable_0[i]) begin
        data[wr_addr_0][i*MASKSIZE +: MASKSIZE] <=
          wr_din_0[i*MASKSIZE +: MASKSIZE];
    end
  end
end

//READ PORT 0 — combinational read of the CURRENT address, then the
//per-write-port FWD mask resolves same-cycle writes (write port j forwards
//iff its FWD bit is set; port 0 has forwarding priority). LATENCY_0==1
//flops the resolved value ONCE at the output (exactly one edge); ==0 is
//fully asynchronous.
reg [BITS-1:0]        d0_mem;
reg [BITS-1:0]        d0_fwd;

always_comb begin
  if (rd_enable_0)
    d0_mem = data[rd_addr_0];
  else
    d0_mem = {BITS{1'bx}};
end

genvar fwd_j0;
generate
  for(fwd_j0=0;fwd_j0<WENSIZE;fwd_j0=fwd_j0+1) begin:FWD_BLOCK_CALC_0
    always_comb begin
      d0_fwd[fwd_j0*MASKSIZE +: MASKSIZE] =
        (((FWD >> 0) & 1) != 0 && wr_enable_0[fwd_j0] && (wr_addr_0 == rd_addr_0)) ?
        wr_din_0[fwd_j0*MASKSIZE +: MASKSIZE] :
        d0_mem[fwd_j0*MASKSIZE +: MASKSIZE];
    end
  end
endgenerate

generate
  if (LATENCY_0==1) begin:BLOCK_RD_LAT_0
    always @(posedge clk) begin
      rd_dout_0 <= d0_fwd;
    end
  end else begin:BLOCK_RD_COMB_0
    assign rd_dout_0 = d0_fwd;
  end
endgenerate

//READ PORT 1 — combinational read of the CURRENT address, then the
//per-write-port FWD mask resolves same-cycle writes (write port j forwards
//iff its FWD bit is set; port 0 has forwarding priority). LATENCY_0==1
//flops the resolved value ONCE at the output (exactly one edge); ==0 is
//fully asynchronous.
reg [BITS-1:0]        d1_mem;
reg [BITS-1:0]        d1_fwd;

always_comb begin
  if (rd_enable_1)
    d1_mem = data[rd_addr_1];
  else
    d1_mem = {BITS{1'bx}};
end

genvar fwd_j1;
generate
  for(fwd_j1=0;fwd_j1<WENSIZE;fwd_j1=fwd_j1+1) begin:FWD_BLOCK_CALC_1
    always_comb begin
      d1_fwd[fwd_j1*MASKSIZE +: MASKSIZE] =
        (((FWD >> 0) & 1) != 0 && wr_enable_0[fwd_j1] && (wr_addr_0 == rd_addr_1)) ?
        wr_din_0[fwd_j1*MASKSIZE +: MASKSIZE] :
        d1_mem[fwd_j1*MASKSIZE +: MASKSIZE];
    end
  end
endgenerate

generate
  if (LATENCY_0==1) begin:BLOCK_RD_LAT_1
    always @(posedge clk) begin
      rd_dout_1 <= d1_fwd;
    end
  end else begin:BLOCK_RD_COMB_1
    assign rd_dout_1 = d1_fwd;
  end
endgenerate

//READ PORT 2 — combinational read of the CURRENT address, then the
//per-write-port FWD mask resolves same-cycle writes (write port j forwards
//iff its FWD bit is set; port 0 has forwarding priority). LATENCY_0==1
//flops the resolved value ONCE at the output (exactly one edge); ==0 is
//fully asynchronous.
reg [BITS-1:0]        d2_mem;
reg [BITS-1:0]        d2_fwd;

always_comb begin
  if (rd_enable_2)
    d2_mem = data[rd_addr_2];
  else
    d2_mem = {BITS{1'bx}};
end

genvar fwd_j2;
generate
  for(fwd_j2=0;fwd_j2<WENSIZE;fwd_j2=fwd_j2+1) begin:FWD_BLOCK_CALC_2
    always_comb begin
      d2_fwd[fwd_j2*MASKSIZE +: MASKSIZE] =
        (((FWD >> 0) & 1) != 0 && wr_enable_0[fwd_j2] && (wr_addr_0 == rd_addr_2)) ?
        wr_din_0[fwd_j2*MASKSIZE +: MASKSIZE] :
        d2_mem[fwd_j2*MASKSIZE +: MASKSIZE];
    end
  end
endgenerate

generate
  if (LATENCY_0==1) begin:BLOCK_RD_LAT_2
    always @(posedge clk) begin
      rd_dout_2 <= d2_fwd;
    end
  end else begin:BLOCK_RD_COMB_2
    assign rd_dout_2 = d2_fwd;
  end
endgenerate

//READ PORT 3 — combinational read of the CURRENT address, then the
//per-write-port FWD mask resolves same-cycle writes (write port j forwards
//iff its FWD bit is set; port 0 has forwarding priority). LATENCY_0==1
//flops the resolved value ONCE at the output (exactly one edge); ==0 is
//fully asynchronous.
reg [BITS-1:0]        d3_mem;
reg [BITS-1:0]        d3_fwd;

always_comb begin
  if (rd_enable_3)
    d3_mem = data[rd_addr_3];
  else
    d3_mem = {BITS{1'bx}};
end

genvar fwd_j3;
generate
  for(fwd_j3=0;fwd_j3<WENSIZE;fwd_j3=fwd_j3+1) begin:FWD_BLOCK_CALC_3
    always_comb begin
      d3_fwd[fwd_j3*MASKSIZE +: MASKSIZE] =
        (((FWD >> 0) & 1) != 0 && wr_enable_0[fwd_j3] && (wr_addr_0 == rd_addr_3)) ?
        wr_din_0[fwd_j3*MASKSIZE +: MASKSIZE] :
        d3_mem[fwd_j3*MASKSIZE +: MASKSIZE];
    end
  end
endgenerate

generate
  if (LATENCY_0==1) begin:BLOCK_RD_LAT_3
    always @(posedge clk) begin
      rd_dout_3 <= d3_fwd;
    end
  end else begin:BLOCK_RD_COMB_3
    assign rd_dout_3 = d3_fwd;
  end
endgenerate

endmodule
