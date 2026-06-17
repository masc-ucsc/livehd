(* blackbox *)
module ResultFlagsSplit#(parameter WIDTH=1)
(
    input RES_UOp[WIDTH-1:0] IN_uop,
    output FlagsUOp[WIDTH-1:0] OUT_flagsUOp,
    output ResultUOp[WIDTH-1:0] OUT_resultUOp
);
endmodule
