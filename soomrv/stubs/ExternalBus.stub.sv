(* blackbox *)
module ExternalBus#(parameter WIDTH=32, parameter ADDR_LEN=32)
(
    input logic clk,
    input logic rst,

    output logic OUT_busOE,
    output logic[WIDTH-1:0] OUT_bus,
    input logic[WIDTH-1:0] IN_bus,
    output logic OUT_busValid,
    input logic IN_busReady,

    input logic[`AXI_ID_LEN-1:0]  s_axi_awid, 
    input logic[ADDR_LEN-1:0] s_axi_awaddr, 
    input logic[7:0] s_axi_awlen, 
    input logic[2:0] s_axi_awsize, 
    input logic[1:0] s_axi_awburst, 
    input logic[0:0] s_axi_awlock, 
    input logic[3:0] s_axi_awcache, 
    input logic s_axi_awvalid,
    output logic s_axi_awready,

    
    input logic [WIDTH-1:0] s_axi_wdata,
    input logic [(WIDTH/8)-1:0] s_axi_wstrb,
    input logic s_axi_wlast,
    input logic s_axi_wvalid,
    output logic s_axi_wready,

    
    input logic s_axi_bready,
    output logic[`AXI_ID_LEN-1:0] s_axi_bid,
    output logic s_axi_bvalid,

    
    input logic[`AXI_ID_LEN-1:0] s_axi_arid,
    input logic[ADDR_LEN-1:0] s_axi_araddr,
    input logic[7:0] s_axi_arlen,
    input logic[2:0] s_axi_arsize,
    input logic[1:0] s_axi_arburst,
    input logic[0:0] s_axi_arlock,
    input logic[3:0] s_axi_arcache,
    input logic s_axi_arvalid,
    output logic s_axi_arready,

    
    input logic s_axi_rready,
    output logic[`AXI_ID_LEN-1:0] s_axi_rid,
    output logic[WIDTH-1:0] s_axi_rdata,
    
    output logic s_axi_rlast,
    output logic s_axi_rvalid
);
endmodule
