(* blackbox *)
module Load
#(
    parameter NUM_UOPS=4,
    parameter NUM_WBS=4,
    parameter NUM_ZC_FWDS=2,
    parameter NUM_PC_READS=2
)
(
    input wire clk,
    input wire rst,

    input IS_UOp IN_uop[NUM_UOPS-1:0],

    
    input ResultUOp IN_resultUOps[NUM_WBS-1:0],

    input BranchProv IN_branch,

    input wire IN_stall[NUM_UOPS-1:0],

    
    input ZCForward IN_zcFwd[NUM_ZC_FWDS-1:0],

    
    output PCFileReadReq OUT_pcRead[NUM_PC_READS-1:0],
    input PCFileEntry IN_pcReadData[NUM_PC_READS-1:0],

    
    output RF_ReadReq[NUM_ALUS*2+NUM_AGUS-1:0] OUT_rfReadReq,
    input wire[NUM_ALUS*2+NUM_AGUS-1:0][31:0] IN_rfReadData,

    output EX_UOp OUT_uop[NUM_UOPS-1:0]
);
endmodule
