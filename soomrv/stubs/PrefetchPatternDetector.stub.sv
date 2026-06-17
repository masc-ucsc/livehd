(* blackbox *)
module PrefetchPatternDetector
#(
    parameter SR_SIZE = 4,
    parameter FIFO_SIZE = 4
)
(
    input wire clk,
    input wire rst,

    input PrefetchMiss IN_miss,
    output PrefetchPattern OUT_pattern
);
endmodule
