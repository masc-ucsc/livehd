(* blackbox *)
module OHEncoder#(parameter LEN=32, parameter ALLOW_NULL=0)
(
    input wire[LEN-1:0] IN_idxOH,
    output reg[LEN == 1 ? 0 : $clog2(LEN)-1:0] OUT_idx,
    output reg OUT_valid
);
endmodule
