(* blackbox *)
module RFReadMux
#(
    parameter VIRT_READS = 4,
    parameter PHY_READS = 3
)
(
    input wire clk,

    input  RF_ReadReq[VIRT_READS-1:0] IN_read,
    output logic[VIRT_READS-1:0] OUT_readReady,
    output RegT[VIRT_READS-1:0] OUT_readData,

    output logic[PHY_READS-1:0] OUT_readEnable,
    output RFTag[PHY_READS-1:0] OUT_readAddress,
    input  RegT[PHY_READS-1:0]  IN_readData
);
endmodule
