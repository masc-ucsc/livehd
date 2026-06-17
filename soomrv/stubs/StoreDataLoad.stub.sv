(* blackbox *)
module StoreDataLoad#(parameter WIDTH=2)
(
    input wire clk,
    input wire rst,

    input BranchProv IN_branch,

    input StDataLookupUOp IN_uop[WIDTH-1:0],
    output logic OUT_ready[WIDTH-1:0],

    input AMO_Data_UOp IN_atomicUOp[WIDTH-1:0],

    output RF_ReadReq[WIDTH-1:0] OUT_readReq,
    input logic[WIDTH-1:0] IN_readReady,
    input RegT[WIDTH-1:0] IN_readData,

    output StDataUOp OUT_uop[WIDTH-1:0]
);
endmodule
