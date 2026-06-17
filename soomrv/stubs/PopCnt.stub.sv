(* blackbox *)
module PopCnt#(parameter SIZE = 32)
(
    input wire[SIZE-1:0] in,
    output wire[$clog2(SIZE):0] res
);
endmodule
