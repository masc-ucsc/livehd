(* blackbox *)
module LoadSelector
(
    input LD_UOp IN_aguLd[NUM_AGUS-1:0],
    output reg OUT_aguLdStall[NUM_AGUS-1:0],

    input PW_LD_UOp IN_pwLd[NUM_AGUS-1:0],
    output reg OUT_pwLdStall[NUM_AGUS-1:0],

    input wire IN_ldUOpStall[NUM_AGUS-1:0],
    output LD_UOp OUT_ldUOp[NUM_AGUS-1:0]
);
endmodule
