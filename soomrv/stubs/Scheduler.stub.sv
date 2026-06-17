(* blackbox *)
module Scheduler
(
    input wire clk,
    input wire rst,

    input wire IN_valid,

    input SqN IN_uopSqN[`DEC_WIDTH-1:0],
    input SqN IN_uopLoadSqN[`DEC_WIDTH-1:0],
    input SqN IN_uopStoreSqN[`DEC_WIDTH-1:0],

    input D_UOp IN_uop[`DEC_WIDTH-1:0],
    output IntUOpOrder_t OUT_order[`DEC_WIDTH-1:0]
);
endmodule
