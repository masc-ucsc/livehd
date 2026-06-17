(* blackbox *)
module PrefixSum#(parameter N = 32)
(
    input logic[N-1:0] IN_data,
    output logic[$clog2(N+1)-1:0] OUT_psum[N-1:0]
);
endmodule
