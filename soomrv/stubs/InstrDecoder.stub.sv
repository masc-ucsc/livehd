(* blackbox *)
module InstrDecoder
#(
    parameter NUM_UOPS=`DEC_WIDTH,
    parameter DO_FUSE=0,
    parameter FUSE_LUI=0,
    parameter FUSE_STORE_DATA=0
)
(
    input wire clk,
    input wire rst,
    input wire en,
    input BranchProv IN_branch,

    input DecodeState IN_dec,
    input PD_Instr IN_instrs[NUM_UOPS-1:0],

    input wire IN_enCustom,

    output DecodeBranch OUT_decBranch,
    output D_UOp OUT_uop[NUM_UOPS-1:0]
);
endmodule
