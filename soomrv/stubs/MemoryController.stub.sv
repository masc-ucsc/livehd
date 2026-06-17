(* blackbox *)
module MemoryController
#(parameter NUM_CACHES=2, parameter NUM_TFS_IN=3, parameter ADDR_LEN=32, parameter WIDTH=`AXI_WIDTH)
(
    input wire clk,
    input wire rst,

    input MemController_Req IN_ctrl[NUM_TFS_IN-1:0],
    output MemController_Res OUT_stat,

    output ICacheIF OUT_icacheW,
    output CacheIF OUT_dcacheW,
    input logic IN_dcacheRReady,
    output CacheIF OUT_dcacheR,
    input wire[32*`CWIDTH-1:0] IN_dcacheR,

    output logic[`AXI_ID_LEN-1:0]  s_axi_awid, 
    output logic[ADDR_LEN-1:0] s_axi_awaddr, 
    output logic[7:0] s_axi_awlen, 
    output logic[2:0] s_axi_awsize, 
    output logic[1:0] s_axi_awburst, 
    output logic[0:0] s_axi_awlock, 
    output logic[3:0] s_axi_awcache, 
    output logic s_axi_awvalid,
    input logic s_axi_awready,

    
    output logic [WIDTH-1:0] s_axi_wdata,
    output logic [(WIDTH/8)-1:0] s_axi_wstrb,
    output logic s_axi_wlast,
    output logic s_axi_wvalid,
    input logic s_axi_wready,

    
    output logic s_axi_bready,
    input logic[`AXI_ID_LEN-1:0] s_axi_bid,
    
    input logic s_axi_bvalid,

    
    output logic[`AXI_ID_LEN-1:0] s_axi_arid,
    output logic[ADDR_LEN-1:0] s_axi_araddr,
    output logic[7:0] s_axi_arlen,
    output logic[2:0] s_axi_arsize,
    output logic[1:0] s_axi_arburst,
    output logic[0:0] s_axi_arlock,
    output logic[3:0] s_axi_arcache, 
    output logic s_axi_arvalid,
    input logic s_axi_arready,

    
    output logic s_axi_rready,
    input logic[`AXI_ID_LEN-1:0] s_axi_rid,
    input logic[WIDTH-1:0] s_axi_rdata,
    
    input logic s_axi_rlast,
    input logic s_axi_rvalid,

    output DebugInfoMemC OUT_dbg
);
endmodule
