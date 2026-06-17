(* blackbox *)
module ExternalAXISim
#(parameter ID_LEN=`AXI_ID_LEN, parameter WIDTH=`AXI_WIDTH, parameter ADDR_LEN=32)
(
    input wire clk,
    input wire rst,

    
    input[ID_LEN-1:0]  s_axi_awid, 
    input[ADDR_LEN-1:0] s_axi_awaddr, 
    input[7:0] s_axi_awlen, 
    input[2:0] s_axi_awsize, 
    input[1:0] s_axi_awburst, 
    input[0:0] s_axi_awlock, 
    input[3:0] s_axi_awcache, 
    input s_axi_awvalid,
    output logic s_axi_awready,

    
    input[WIDTH-1:0] s_axi_wdata,
    input[(WIDTH/8)-1:0] s_axi_wstrb,
    input s_axi_wlast,
    input s_axi_wvalid,
    output logic s_axi_wready,

    
    input s_axi_bready,
    output logic[ID_LEN-1:0] s_axi_bid,
    
    output logic s_axi_bvalid,

    
    input[ID_LEN-1:0] s_axi_arid,
    input[ADDR_LEN-1:0] s_axi_araddr,
    input[7:0] s_axi_arlen,
    input[2:0] s_axi_arsize,
    input[1:0] s_axi_arburst,
    input[0:0] s_axi_arlock,
    input[3:0] s_axi_arcache, 
    input s_axi_arvalid,
    output logic s_axi_arready,

    
    input s_axi_rready,
    output logic[ID_LEN-1:0] s_axi_rid,
    output logic[WIDTH-1:0] s_axi_rdata,
    
    output logic s_axi_rlast,
    output logic s_axi_rvalid
);
endmodule
