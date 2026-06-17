(* blackbox *)
module RangeMaskGen#(parameter LENGTH=16, parameter OUTPUT_ON_EQUAL=0, parameter START_SHIFT=0, parameter END_SHIFT=0)
(
    input wire IN_allOnes,
    input wire IN_enable,
    input wire[$clog2(LENGTH)-1:0] IN_startIdx,
    input wire[$clog2(LENGTH)-1:0] IN_endIdx,
    output logic[LENGTH-1:0] OUT_range
);
endmodule
