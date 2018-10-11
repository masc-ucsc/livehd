/* inputs:
   ------------
 */

/* outputs:
   ------------
  debug_reg_addr                : logic[63:0]
  debug_reg_data                : logic[63:0]
  debug_committed_pc            : logic[63:0] */


module pipe(




  output [64-1:0] debug_reg_addr_pyro,
  output debug_reg_addr_valid_pyro,
  input debug_reg_addr_retry_pyri,

  output [64-1:0] debug_reg_data_pyro,
  output debug_reg_data_valid_pyro,
  input debug_reg_data_retry_pyri,

  output [64-1:0] debug_committed_pc_pyro,
  output debug_committed_pc_valid_pyro,
  input debug_committed_pc_retry_pyri,

  
  input clk,
  input reset
);

logic reset_pyri;
assign reset_pyri = reset;

logic [63:0] wb__debug_reg_addr_pyro;
logic wb__debug_reg_addr_valid_pyro;
logic wb__debug_reg_addr_retry_pyri;
logic [63:0] wb__debug_reg_data_pyro;
logic wb__debug_reg_data_valid_pyro;
logic wb__debug_reg_data_retry_pyri;
logic [63:0] wb__pc_pyro;
logic wb__pc_valid_pyro;
logic wb__pc_retry_pyri;
logic [63:0] wb__branch_pc_pyro;
logic wb__branch_pc_valid_pyro;
logic wb__branch_pc_retry_pyri;
logic [63:0] fetch__branch_pc_pyri;
logic fetch__branch_pc_valid_pyri;
logic fetch__branch_pc_retry_pyro;
logic [63:0] wb__decode_reg_addr_pyro;
logic wb__decode_reg_addr_valid_pyro;
logic wb__decode_reg_addr_retry_pyri;
logic [63:0] decode__reg_addr_pyri;
logic decode__reg_addr_valid_pyri;
logic decode__reg_addr_retry_pyro;
logic [63:0] wb__decode_reg_data_pyro;
logic wb__decode_reg_data_valid_pyro;
logic wb__decode_reg_data_retry_pyri;
logic [63:0] decode__reg_data_pyri;
logic decode__reg_data_valid_pyri;
logic decode__reg_data_retry_pyro;
logic [31:0] fetch__inst_pyro;
logic fetch__inst_valid_pyro;
logic fetch__inst_retry_pyri;
logic [31:0] decode__inst_pyri;
logic decode__inst_valid_pyri;
logic decode__inst_retry_pyro;
logic [63:0] fetch__pc_pyro;
logic fetch__pc_valid_pyro;
logic fetch__pc_retry_pyri;
logic [63:0] decode__pc_pyri;
logic decode__pc_valid_pyri;
logic decode__pc_retry_pyro;
logic [31:0] decode__inst_pyro;
logic decode__inst_valid_pyro;
logic decode__inst_retry_pyri;
logic [31:0] execute__inst_pyri;
logic execute__inst_valid_pyri;
logic execute__inst_retry_pyro;
logic [63:0] decode__src1_pyro;
logic decode__src1_valid_pyro;
logic decode__src1_retry_pyri;
logic [63:0] execute__in1_pyri;
logic execute__in1_valid_pyri;
logic execute__in1_retry_pyro;
logic [63:0] decode__src2_pyro;
logic decode__src2_valid_pyro;
logic decode__src2_retry_pyri;
logic [63:0] execute__in2_pyri;
logic execute__in2_valid_pyri;
logic execute__in2_retry_pyro;
logic [63:0] decode__pc_pyro;
logic decode__pc_valid_pyro;
logic decode__pc_retry_pyri;
logic [63:0] execute__pc_pyri;
logic execute__pc_valid_pyri;
logic execute__pc_retry_pyro;
logic [31:0] execute__inst_pyro;
logic execute__inst_valid_pyro;
logic execute__inst_retry_pyri;
logic [31:0] wb__inst_pyri;
logic wb__inst_valid_pyri;
logic wb__inst_retry_pyro;
logic [63:0] execute__pc_pyro;
logic execute__pc_valid_pyro;
logic execute__pc_retry_pyri;
logic [63:0] wb__pc_pyri;
logic wb__pc_valid_pyri;
logic wb__pc_retry_pyro;
logic [63:0] execute__raddr_pyro;
logic execute__raddr_valid_pyro;
logic execute__raddr_retry_pyri;
logic [63:0] wb__raddr_pyri;
logic wb__raddr_valid_pyri;
logic wb__raddr_retry_pyro;
logic [63:0] execute__rdata_pyro;
logic execute__rdata_valid_pyro;
logic execute__rdata_retry_pyri;
logic [63:0] wb__rdata_pyri;
logic wb__rdata_valid_pyri;
logic wb__rdata_retry_pyro;
logic [63:0] execute__branch_pc_pyro;
logic execute__branch_pc_valid_pyro;
logic execute__branch_pc_retry_pyri;
logic [63:0] wb__branch_pc_pyri;
logic wb__branch_pc_valid_pyri;
logic wb__branch_pc_retry_pyro;


fflop #(64) __global__debug_reg_addr(.q(debug_reg_addr_pyro),
.qValid(debug_reg_addr_valid_pyro),
.qRetry(debug_reg_addr_retry_pyri),
.din(wb__debug_reg_addr_pyro),
.dinValid(wb__debug_reg_addr_valid_pyro),
.dinRetry(wb__debug_reg_addr_retry_pyri),

.clk(clk), .reset(reset_pyri));
fflop #(64) __global__debug_reg_data(.q(debug_reg_data_pyro),
.qValid(debug_reg_data_valid_pyro),
.qRetry(debug_reg_data_retry_pyri),
.din(wb__debug_reg_data_pyro),
.dinValid(wb__debug_reg_data_valid_pyro),
.dinRetry(wb__debug_reg_data_retry_pyri),

.clk(clk), .reset(reset_pyri));
fflop #(64) __global__debug_committed_pc(.q(debug_committed_pc_pyro),
.qValid(debug_committed_pc_valid_pyro),
.qRetry(debug_committed_pc_retry_pyri),
.din(wb__pc_pyro),
.dinValid(wb__pc_valid_pyro),
.dinRetry(wb__pc_retry_pyri),

.clk(clk), .reset(reset_pyri));
fflop #(64) fetch__branch_pc(.din(wb__branch_pc_pyro),
.dinValid(wb__branch_pc_valid_pyro),
.dinRetry(wb__branch_pc_retry_pyri),
.q(fetch__branch_pc_pyri),
.qValid(fetch__branch_pc_valid_pyri),
.qRetry(fetch__branch_pc_retry_pyro),

.clk(clk), .reset(reset_pyri));
fflop #(64) decode__reg_addr(.din(wb__decode_reg_addr_pyro),
.dinValid(wb__decode_reg_addr_valid_pyro),
.dinRetry(wb__decode_reg_addr_retry_pyri),
.q(decode__reg_addr_pyri),
.qValid(decode__reg_addr_valid_pyri),
.qRetry(decode__reg_addr_retry_pyro),

.clk(clk), .reset(reset_pyri));
fflop #(64) decode__reg_data(.din(wb__decode_reg_data_pyro),
.dinValid(wb__decode_reg_data_valid_pyro),
.dinRetry(wb__decode_reg_data_retry_pyri),
.q(decode__reg_data_pyri),
.qValid(decode__reg_data_valid_pyri),
.qRetry(decode__reg_data_retry_pyro),

.clk(clk), .reset(reset_pyri));
fflop #(32) decode__inst(.din(fetch__inst_pyro),
.dinValid(fetch__inst_valid_pyro),
.dinRetry(fetch__inst_retry_pyri),
.q(decode__inst_pyri),
.qValid(decode__inst_valid_pyri),
.qRetry(decode__inst_retry_pyro),

.clk(clk), .reset(reset_pyri));
fflop #(64) decode__pc(.din(fetch__pc_pyro),
.dinValid(fetch__pc_valid_pyro),
.dinRetry(fetch__pc_retry_pyri),
.q(decode__pc_pyri),
.qValid(decode__pc_valid_pyri),
.qRetry(decode__pc_retry_pyro),

.clk(clk), .reset(reset_pyri));
fflop #(32) execute__inst(.din(decode__inst_pyro),
.dinValid(decode__inst_valid_pyro),
.dinRetry(decode__inst_retry_pyri),
.q(execute__inst_pyri),
.qValid(execute__inst_valid_pyri),
.qRetry(execute__inst_retry_pyro),

.clk(clk), .reset(reset_pyri));
fflop #(64) execute__in1(.din(decode__src1_pyro),
.dinValid(decode__src1_valid_pyro),
.dinRetry(decode__src1_retry_pyri),
.q(execute__in1_pyri),
.qValid(execute__in1_valid_pyri),
.qRetry(execute__in1_retry_pyro),

.clk(clk), .reset(reset_pyri));
fflop #(64) execute__in2(.din(decode__src2_pyro),
.dinValid(decode__src2_valid_pyro),
.dinRetry(decode__src2_retry_pyri),
.q(execute__in2_pyri),
.qValid(execute__in2_valid_pyri),
.qRetry(execute__in2_retry_pyro),

.clk(clk), .reset(reset_pyri));
fflop #(64) execute__pc(.din(decode__pc_pyro),
.dinValid(decode__pc_valid_pyro),
.dinRetry(decode__pc_retry_pyri),
.q(execute__pc_pyri),
.qValid(execute__pc_valid_pyri),
.qRetry(execute__pc_retry_pyro),

.clk(clk), .reset(reset_pyri));
fflop #(32) wb__inst(.din(execute__inst_pyro),
.dinValid(execute__inst_valid_pyro),
.dinRetry(execute__inst_retry_pyri),
.q(wb__inst_pyri),
.qValid(wb__inst_valid_pyri),
.qRetry(wb__inst_retry_pyro),

.clk(clk), .reset(reset_pyri));
fflop #(64) wb__pc(.din(execute__pc_pyro),
.dinValid(execute__pc_valid_pyro),
.dinRetry(execute__pc_retry_pyri),
.q(wb__pc_pyri),
.qValid(wb__pc_valid_pyri),
.qRetry(wb__pc_retry_pyro),

.clk(clk), .reset(reset_pyri));
fflop #(64) wb__raddr(.din(execute__raddr_pyro),
.dinValid(execute__raddr_valid_pyro),
.dinRetry(execute__raddr_retry_pyri),
.q(wb__raddr_pyri),
.qValid(wb__raddr_valid_pyri),
.qRetry(wb__raddr_retry_pyro),

.clk(clk), .reset(reset_pyri));
fflop #(64) wb__rdata(.din(execute__rdata_pyro),
.dinValid(execute__rdata_valid_pyro),
.dinRetry(execute__rdata_retry_pyri),
.q(wb__rdata_pyri),
.qValid(wb__rdata_valid_pyri),
.qRetry(wb__rdata_retry_pyro),

.clk(clk), .reset(reset_pyri));
fflop #(64) wb__branch_pc(.din(execute__branch_pc_pyro),
.dinValid(execute__branch_pc_valid_pyro),
.dinRetry(execute__branch_pc_retry_pyri),
.q(wb__branch_pc_pyri),
.qValid(wb__branch_pc_valid_pyri),
.qRetry(wb__branch_pc_retry_pyro),

.clk(clk), .reset(reset_pyri));



  pyrm_fetch_block fetch(.branch_pc_pyri(fetch__branch_pc_pyri),
.branch_pc_valid_pyri(fetch__branch_pc_valid_pyri),
.branch_pc_retry_pyro(fetch__branch_pc_retry_pyro),
.inst_pyro(fetch__inst_pyro),
.inst_valid_pyro(fetch__inst_valid_pyro),
.inst_retry_pyri(fetch__inst_retry_pyri),
.pc_pyro(fetch__pc_pyro),
.pc_valid_pyro(fetch__pc_valid_pyro),
.pc_retry_pyri(fetch__pc_retry_pyri),
.clk(clk), .reset_pyri(reset_pyri));

  pyrm_decode_block decode(.reg_addr_pyri(decode__reg_addr_pyri),
.reg_addr_valid_pyri(decode__reg_addr_valid_pyri),
.reg_addr_retry_pyro(decode__reg_addr_retry_pyro),
.reg_data_pyri(decode__reg_data_pyri),
.reg_data_valid_pyri(decode__reg_data_valid_pyri),
.reg_data_retry_pyro(decode__reg_data_retry_pyro),
.inst_pyri(decode__inst_pyri),
.inst_valid_pyri(decode__inst_valid_pyri),
.inst_retry_pyro(decode__inst_retry_pyro),
.pc_pyri(decode__pc_pyri),
.pc_valid_pyri(decode__pc_valid_pyri),
.pc_retry_pyro(decode__pc_retry_pyro),
.inst_pyro(decode__inst_pyro),
.inst_valid_pyro(decode__inst_valid_pyro),
.inst_retry_pyri(decode__inst_retry_pyri),
.src1_pyro(decode__src1_pyro),
.src1_valid_pyro(decode__src1_valid_pyro),
.src1_retry_pyri(decode__src1_retry_pyri),
.src2_pyro(decode__src2_pyro),
.src2_valid_pyro(decode__src2_valid_pyro),
.src2_retry_pyri(decode__src2_retry_pyri),
.pc_pyro(decode__pc_pyro),
.pc_valid_pyro(decode__pc_valid_pyro),
.pc_retry_pyri(decode__pc_retry_pyri),
.clk(clk), .reset_pyri(reset_pyri));

  pyrm_execute_block execute(.inst_pyri(execute__inst_pyri),
.inst_valid_pyri(execute__inst_valid_pyri),
.inst_retry_pyro(execute__inst_retry_pyro),
.in1_pyri(execute__in1_pyri),
.in1_valid_pyri(execute__in1_valid_pyri),
.in1_retry_pyro(execute__in1_retry_pyro),
.in2_pyri(execute__in2_pyri),
.in2_valid_pyri(execute__in2_valid_pyri),
.in2_retry_pyro(execute__in2_retry_pyro),
.pc_pyri(execute__pc_pyri),
.pc_valid_pyri(execute__pc_valid_pyri),
.pc_retry_pyro(execute__pc_retry_pyro),
.inst_pyro(execute__inst_pyro),
.inst_valid_pyro(execute__inst_valid_pyro),
.inst_retry_pyri(execute__inst_retry_pyri),
.pc_pyro(execute__pc_pyro),
.pc_valid_pyro(execute__pc_valid_pyro),
.pc_retry_pyri(execute__pc_retry_pyri),
.raddr_pyro(execute__raddr_pyro),
.raddr_valid_pyro(execute__raddr_valid_pyro),
.raddr_retry_pyri(execute__raddr_retry_pyri),
.rdata_pyro(execute__rdata_pyro),
.rdata_valid_pyro(execute__rdata_valid_pyro),
.rdata_retry_pyri(execute__rdata_retry_pyri),
.branch_pc_pyro(execute__branch_pc_pyro),
.branch_pc_valid_pyro(execute__branch_pc_valid_pyro),
.branch_pc_retry_pyri(execute__branch_pc_retry_pyri),
.clk(clk), .reset_pyri(reset_pyri));

  pyrm_write_back_block wb(.debug_reg_addr_pyro(wb__debug_reg_addr_pyro),
.debug_reg_addr_valid_pyro(wb__debug_reg_addr_valid_pyro),
.debug_reg_addr_retry_pyri(wb__debug_reg_addr_retry_pyri),
.debug_reg_data_pyro(wb__debug_reg_data_pyro),
.debug_reg_data_valid_pyro(wb__debug_reg_data_valid_pyro),
.debug_reg_data_retry_pyri(wb__debug_reg_data_retry_pyri),
.pc_pyro(wb__pc_pyro),
.pc_valid_pyro(wb__pc_valid_pyro),
.pc_retry_pyri(wb__pc_retry_pyri),
.branch_pc_pyro(wb__branch_pc_pyro),
.branch_pc_valid_pyro(wb__branch_pc_valid_pyro),
.branch_pc_retry_pyri(wb__branch_pc_retry_pyri),
.decode_reg_addr_pyro(wb__decode_reg_addr_pyro),
.decode_reg_addr_valid_pyro(wb__decode_reg_addr_valid_pyro),
.decode_reg_addr_retry_pyri(wb__decode_reg_addr_retry_pyri),
.decode_reg_data_pyro(wb__decode_reg_data_pyro),
.decode_reg_data_valid_pyro(wb__decode_reg_data_valid_pyro),
.decode_reg_data_retry_pyri(wb__decode_reg_data_retry_pyri),
.inst_pyri(wb__inst_pyri),
.inst_valid_pyri(wb__inst_valid_pyri),
.inst_retry_pyro(wb__inst_retry_pyro),
.pc_pyri(wb__pc_pyri),
.pc_valid_pyri(wb__pc_valid_pyri),
.pc_retry_pyro(wb__pc_retry_pyro),
.raddr_pyri(wb__raddr_pyri),
.raddr_valid_pyri(wb__raddr_valid_pyri),
.raddr_retry_pyro(wb__raddr_retry_pyro),
.rdata_pyri(wb__rdata_pyri),
.rdata_valid_pyri(wb__rdata_valid_pyri),
.rdata_retry_pyro(wb__rdata_retry_pyro),
.branch_pc_pyri(wb__branch_pc_pyri),
.branch_pc_valid_pyri(wb__branch_pc_valid_pyri),
.branch_pc_retry_pyro(wb__branch_pc_retry_pyro),
.clk(clk), .reset_pyri(reset_pyri));


endmodule
