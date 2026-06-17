(* blackbox *)
module ACLINT
(
    input wire clk,
    input wire rst,

    input wire IN_re,
    input wire[29:0] IN_raddr,
    output reg[31:0] OUT_rdata,
    output wire OUT_rbusy,
    output reg OUT_rvalid,

    input wire IN_we,
    input wire[3:0] IN_wmask,
    input wire[29:0] IN_waddr,
    input wire[31:0] IN_wdata,

    output wire[63:0] OUT_mtime,
    output wire[63:0] OUT_mtimecmp

);
endmodule
