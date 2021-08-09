module cgen_driver(
    input clk
    ,input [9:0]   rd_addr_0, rd_addr_1, rd_addr_2, rd_addr_3, rd_addr_4
    ,input         rd_enable_0, rd_enable_1, rd_enable_2, rd_enable_3, rd_enable_4
    ,output [15:0] rd_dout_0, rd_dout_1, rd_dout_2, rd_dout_3, rd_dout_4
    ,input [9:0]   wr_addr_0, wr_addr_1, wr_addr_2, wr_addr_3, wr_addr_4
    ,input         wr_enable_0, wr_enable_1, wr_enable_2, wr_enable_3
    ,input [1:0]   wr_enable_4
    ,input [15:0]  wr_din_0, wr_din_1, wr_din_2, wr_din_3, wr_din_4
);

cgen_memory_1rd_1wr #(.BITS(16), .SIZE(1024), .FWD(0), .LATENCY_0(0), .WENSIZE(1)) c_mem_0(.clk(clk), .rd_addr_0(rd_addr_0), 
    .rd_enable_0(rd_enable_0), .rd_dout_0(rd_dout_0), 
    .wr_addr_0(wr_addr_0), .wr_enable_0(wr_enable_0), 
    .wr_din_0(wr_din_0));

cgen_memory_1rd_1wr #(.BITS(16), .SIZE(1024), .FWD(0), .LATENCY_0(1), .WENSIZE(1)) c_mem_1(.clk(clk), .rd_addr_0(rd_addr_1), 
    .rd_enable_0(rd_enable_1), .rd_dout_0(rd_dout_1), 
    .wr_addr_0(wr_addr_1), .wr_enable_0(wr_enable_1), 
    .wr_din_0(wr_din_1));

cgen_memory_1rd_1wr #(.BITS(16), .SIZE(1024), .FWD(1), .LATENCY_0(0), .WENSIZE(1)) c_mem_2(.clk(clk), .rd_addr_0(rd_addr_2), 
    .rd_enable_0(rd_enable_2), .rd_dout_0(rd_dout_2), 
    .wr_addr_0(wr_addr_2), .wr_enable_0(wr_enable_2), 
    .wr_din_0(wr_din_2));

cgen_memory_1rd_1wr #(.BITS(16), .SIZE(1024), .FWD(1), .LATENCY_0(1), .WENSIZE(1)) c_mem_3(.clk(clk), .rd_addr_0(rd_addr_3), 
    .rd_enable_0(rd_enable_3), .rd_dout_0(rd_dout_3), 
    .wr_addr_0(wr_addr_3), .wr_enable_0(wr_enable_3), 
    .wr_din_0(wr_din_3));

cgen_memory_1rd_1wr #(.BITS(16), .SIZE(1024), .FWD(1), .LATENCY_0(1), .WENSIZE(2)) c_mem_4(.clk(clk), .rd_addr_0(rd_addr_4), 
    .rd_enable_0(rd_enable_4), .rd_dout_0(rd_dout_4), 
    .wr_addr_0(wr_addr_4), .wr_enable_0(wr_enable_4), 
    .wr_din_0(wr_din_4));

endmodule