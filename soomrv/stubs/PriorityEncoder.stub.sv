(* blackbox *)
module PriorityEncoder
#(
    parameter BITS=32,
    parameter N=1
)
(
    input logic[BITS-1:0] IN_data,
    output logic[$clog2(BITS)-1:0] OUT_idx[N-1:0],
    output logic OUT_idxValid[N-1:0]
);
endmodule
