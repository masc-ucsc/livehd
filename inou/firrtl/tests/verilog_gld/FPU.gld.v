module FPU(
  input         clock,
  input         reset,
  input  [31:0] io_inst,
  input  [63:0] io_fromint_data,
  input  [2:0]  io_fcsr_rm,
  output        io_fcsr_flags_valid,
  output [4:0]  io_fcsr_flags_bits,
  output [63:0] io_store_data,
  output [63:0] io_toint_data,
  input         io_dmem_resp_val,
  input  [2:0]  io_dmem_resp_type,
  input  [4:0]  io_dmem_resp_tag,
  input  [63:0] io_dmem_resp_data,
  input         io_valid,
  output        io_fcsr_rdy,
  output        io_nack_mem,
  output        io_illegal_rm,
  input         io_killx,
  input         io_killm,
  output [4:0]  io_dec_cmd,
  output        io_dec_ldst,
  output        io_dec_wen,
  output        io_dec_ren1,
  output        io_dec_ren2,
  output        io_dec_ren3,
  output        io_dec_swap12,
  output        io_dec_swap23,
  output        io_dec_single,
  output        io_dec_fromint,
  output        io_dec_toint,
  output        io_dec_fastpipe,
  output        io_dec_fma,
  output        io_dec_div,
  output        io_dec_sqrt,
  output        io_dec_wflags,
  output        io_sboard_set,
  output        io_sboard_clr,
  output [4:0]  io_sboard_clra,
  output        io_cp_req_ready,
  input         io_cp_req_valid,
  input  [4:0]  io_cp_req_bits_cmd,
  input         io_cp_req_bits_ldst,
  input         io_cp_req_bits_wen,
  input         io_cp_req_bits_ren1,
  input         io_cp_req_bits_ren2,
  input         io_cp_req_bits_ren3,
  input         io_cp_req_bits_swap12,
  input         io_cp_req_bits_swap23,
  input         io_cp_req_bits_single,
  input         io_cp_req_bits_fromint,
  input         io_cp_req_bits_toint,
  input         io_cp_req_bits_fastpipe,
  input         io_cp_req_bits_fma,
  input         io_cp_req_bits_div,
  input         io_cp_req_bits_sqrt,
  input         io_cp_req_bits_wflags,
  input  [2:0]  io_cp_req_bits_rm,
  input  [1:0]  io_cp_req_bits_typ,
  input  [64:0] io_cp_req_bits_in1,
  input  [64:0] io_cp_req_bits_in2,
  input  [64:0] io_cp_req_bits_in3,
  input         io_cp_resp_ready,
  output        io_cp_resp_valid,
  output [64:0] io_cp_resp_bits_data,
  output [4:0]  io_cp_resp_bits_exc
);
`ifdef RANDOMIZE_MEM_INIT
  reg [95:0] _RAND_0;
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_3;
  reg [31:0] _RAND_4;
  reg [31:0] _RAND_5;
  reg [31:0] _RAND_6;
  reg [31:0] _RAND_7;
  reg [31:0] _RAND_8;
  reg [31:0] _RAND_9;
  reg [31:0] _RAND_10;
  reg [31:0] _RAND_11;
  reg [31:0] _RAND_12;
  reg [31:0] _RAND_13;
  reg [31:0] _RAND_14;
  reg [31:0] _RAND_15;
  reg [31:0] _RAND_16;
  reg [31:0] _RAND_17;
  reg [31:0] _RAND_18;
  reg [31:0] _RAND_19;
  reg [31:0] _RAND_20;
  reg [31:0] _RAND_21;
  reg [31:0] _RAND_22;
  reg [31:0] _RAND_23;
  reg [31:0] _RAND_24;
  reg [31:0] _RAND_25;
  reg [31:0] _RAND_26;
  reg [31:0] _RAND_27;
  reg [31:0] _RAND_28;
  reg [31:0] _RAND_29;
  reg [31:0] _RAND_30;
  reg [63:0] _RAND_31;
  reg [31:0] _RAND_32;
  reg [31:0] _RAND_33;
  reg [31:0] _RAND_34;
  reg [31:0] _RAND_35;
  reg [31:0] _RAND_36;
  reg [31:0] _RAND_37;
  reg [31:0] _RAND_38;
  reg [31:0] _RAND_39;
  reg [31:0] _RAND_40;
  reg [31:0] _RAND_41;
  reg [31:0] _RAND_42;
  reg [31:0] _RAND_43;
  reg [31:0] _RAND_44;
  reg [31:0] _RAND_45;
  reg [31:0] _RAND_46;
  reg [31:0] _RAND_47;
  reg [31:0] _RAND_48;
  reg [31:0] _RAND_49;
  reg [31:0] _RAND_50;
  reg [31:0] _RAND_51;
  reg [31:0] _RAND_52;
  reg [31:0] _RAND_53;
  reg [31:0] _RAND_54;
  reg [95:0] _RAND_55;
  reg [31:0] _RAND_56;
  reg [31:0] _RAND_57;
  reg [31:0] _RAND_58;
  reg [31:0] _RAND_59;
`endif // RANDOMIZE_REG_INIT
  wire [31:0] fp_decoder_io_inst; // @[FPU.scala 520:26]
  wire [4:0] fp_decoder_io_sigs_cmd; // @[FPU.scala 520:26]
  wire  fp_decoder_io_sigs_ldst; // @[FPU.scala 520:26]
  wire  fp_decoder_io_sigs_wen; // @[FPU.scala 520:26]
  wire  fp_decoder_io_sigs_ren1; // @[FPU.scala 520:26]
  wire  fp_decoder_io_sigs_ren2; // @[FPU.scala 520:26]
  wire  fp_decoder_io_sigs_ren3; // @[FPU.scala 520:26]
  wire  fp_decoder_io_sigs_swap12; // @[FPU.scala 520:26]
  wire  fp_decoder_io_sigs_swap23; // @[FPU.scala 520:26]
  wire  fp_decoder_io_sigs_single; // @[FPU.scala 520:26]
  wire  fp_decoder_io_sigs_fromint; // @[FPU.scala 520:26]
  wire  fp_decoder_io_sigs_toint; // @[FPU.scala 520:26]
  wire  fp_decoder_io_sigs_fastpipe; // @[FPU.scala 520:26]
  wire  fp_decoder_io_sigs_fma; // @[FPU.scala 520:26]
  wire  fp_decoder_io_sigs_div; // @[FPU.scala 520:26]
  wire  fp_decoder_io_sigs_sqrt; // @[FPU.scala 520:26]
  wire  fp_decoder_io_sigs_wflags; // @[FPU.scala 520:26]
  reg [64:0] regfile [0:31]; // @[FPU.scala 547:20]
  wire [64:0] regfile__T_849_data; // @[FPU.scala 547:20]
  wire [4:0] regfile__T_849_addr; // @[FPU.scala 547:20]
  wire [64:0] regfile__T_853_data; // @[FPU.scala 547:20]
  wire [4:0] regfile__T_853_addr; // @[FPU.scala 547:20]
  wire [64:0] regfile__T_857_data; // @[FPU.scala 547:20]
  wire [4:0] regfile__T_857_addr; // @[FPU.scala 547:20]
  wire [64:0] regfile__T_782_data; // @[FPU.scala 547:20]
  wire [4:0] regfile__T_782_addr; // @[FPU.scala 547:20]
  wire  regfile__T_782_mask; // @[FPU.scala 547:20]
  wire  regfile__T_782_en; // @[FPU.scala 547:20]
  wire [64:0] regfile__T_1131_data; // @[FPU.scala 547:20]
  wire [4:0] regfile__T_1131_addr; // @[FPU.scala 547:20]
  wire  regfile__T_1131_mask; // @[FPU.scala 547:20]
  wire  regfile__T_1131_en; // @[FPU.scala 547:20]
  wire  sfma_clock; // @[FPU.scala 584:20]
  wire  sfma_reset; // @[FPU.scala 584:20]
  wire  sfma_io_in_valid; // @[FPU.scala 584:20]
  wire [4:0] sfma_io_in_bits_cmd; // @[FPU.scala 584:20]
  wire  sfma_io_in_bits_ren3; // @[FPU.scala 584:20]
  wire  sfma_io_in_bits_swap23; // @[FPU.scala 584:20]
  wire [2:0] sfma_io_in_bits_rm; // @[FPU.scala 584:20]
  wire [64:0] sfma_io_in_bits_in1; // @[FPU.scala 584:20]
  wire [64:0] sfma_io_in_bits_in2; // @[FPU.scala 584:20]
  wire [64:0] sfma_io_in_bits_in3; // @[FPU.scala 584:20]
  wire [64:0] sfma_io_out_bits_data; // @[FPU.scala 584:20]
  wire [4:0] sfma_io_out_bits_exc; // @[FPU.scala 584:20]
  wire  fpiu_clock; // @[FPU.scala 588:20]
  wire  fpiu_io_in_valid; // @[FPU.scala 588:20]
  wire [4:0] fpiu_io_in_bits_cmd; // @[FPU.scala 588:20]
  wire  fpiu_io_in_bits_ldst; // @[FPU.scala 588:20]
  wire  fpiu_io_in_bits_single; // @[FPU.scala 588:20]
  wire [2:0] fpiu_io_in_bits_rm; // @[FPU.scala 588:20]
  wire [1:0] fpiu_io_in_bits_typ; // @[FPU.scala 588:20]
  wire [64:0] fpiu_io_in_bits_in1; // @[FPU.scala 588:20]
  wire [64:0] fpiu_io_in_bits_in2; // @[FPU.scala 588:20]
  wire [2:0] fpiu_io_as_double_rm; // @[FPU.scala 588:20]
  wire [64:0] fpiu_io_as_double_in1; // @[FPU.scala 588:20]
  wire [64:0] fpiu_io_as_double_in2; // @[FPU.scala 588:20]
  wire  fpiu_io_out_valid; // @[FPU.scala 588:20]
  wire  fpiu_io_out_bits_lt; // @[FPU.scala 588:20]
  wire [63:0] fpiu_io_out_bits_store; // @[FPU.scala 588:20]
  wire [63:0] fpiu_io_out_bits_toint; // @[FPU.scala 588:20]
  wire [4:0] fpiu_io_out_bits_exc; // @[FPU.scala 588:20]
  wire  ifpu_clock; // @[FPU.scala 598:20]
  wire  ifpu_reset; // @[FPU.scala 598:20]
  wire  ifpu_io_in_valid; // @[FPU.scala 598:20]
  wire [4:0] ifpu_io_in_bits_cmd; // @[FPU.scala 598:20]
  wire  ifpu_io_in_bits_single; // @[FPU.scala 598:20]
  wire [2:0] ifpu_io_in_bits_rm; // @[FPU.scala 598:20]
  wire [1:0] ifpu_io_in_bits_typ; // @[FPU.scala 598:20]
  wire [64:0] ifpu_io_in_bits_in1; // @[FPU.scala 598:20]
  wire [64:0] ifpu_io_out_bits_data; // @[FPU.scala 598:20]
  wire [4:0] ifpu_io_out_bits_exc; // @[FPU.scala 598:20]
  wire  fpmu_clock; // @[FPU.scala 603:20]
  wire  fpmu_reset; // @[FPU.scala 603:20]
  wire  fpmu_io_in_valid; // @[FPU.scala 603:20]
  wire [4:0] fpmu_io_in_bits_cmd; // @[FPU.scala 603:20]
  wire  fpmu_io_in_bits_single; // @[FPU.scala 603:20]
  wire [2:0] fpmu_io_in_bits_rm; // @[FPU.scala 603:20]
  wire [64:0] fpmu_io_in_bits_in1; // @[FPU.scala 603:20]
  wire [64:0] fpmu_io_in_bits_in2; // @[FPU.scala 603:20]
  wire [64:0] fpmu_io_out_bits_data; // @[FPU.scala 603:20]
  wire [4:0] fpmu_io_out_bits_exc; // @[FPU.scala 603:20]
  wire  fpmu_io_lt; // @[FPU.scala 603:20]
  wire  FPUFMAPipe_clock; // @[FPU.scala 624:28]
  wire  FPUFMAPipe_reset; // @[FPU.scala 624:28]
  wire  FPUFMAPipe_io_in_valid; // @[FPU.scala 624:28]
  wire [4:0] FPUFMAPipe_io_in_bits_cmd; // @[FPU.scala 624:28]
  wire  FPUFMAPipe_io_in_bits_ren3; // @[FPU.scala 624:28]
  wire  FPUFMAPipe_io_in_bits_swap23; // @[FPU.scala 624:28]
  wire [2:0] FPUFMAPipe_io_in_bits_rm; // @[FPU.scala 624:28]
  wire [64:0] FPUFMAPipe_io_in_bits_in1; // @[FPU.scala 624:28]
  wire [64:0] FPUFMAPipe_io_in_bits_in2; // @[FPU.scala 624:28]
  wire [64:0] FPUFMAPipe_io_in_bits_in3; // @[FPU.scala 624:28]
  wire [64:0] FPUFMAPipe_io_out_bits_data; // @[FPU.scala 624:28]
  wire [4:0] FPUFMAPipe_io_out_bits_exc; // @[FPU.scala 624:28]
  wire  DivSqrtRecF64_clock; // @[FPU.scala 722:25]
  wire  DivSqrtRecF64_reset; // @[FPU.scala 722:25]
  wire  DivSqrtRecF64_io_inReady_div; // @[FPU.scala 722:25]
  wire  DivSqrtRecF64_io_inReady_sqrt; // @[FPU.scala 722:25]
  wire  DivSqrtRecF64_io_inValid; // @[FPU.scala 722:25]
  wire  DivSqrtRecF64_io_sqrtOp; // @[FPU.scala 722:25]
  wire [64:0] DivSqrtRecF64_io_a; // @[FPU.scala 722:25]
  wire [64:0] DivSqrtRecF64_io_b; // @[FPU.scala 722:25]
  wire [1:0] DivSqrtRecF64_io_roundingMode; // @[FPU.scala 722:25]
  wire  DivSqrtRecF64_io_outValid_div; // @[FPU.scala 722:25]
  wire  DivSqrtRecF64_io_outValid_sqrt; // @[FPU.scala 722:25]
  wire [64:0] DivSqrtRecF64_io_out; // @[FPU.scala 722:25]
  wire [4:0] DivSqrtRecF64_io_exceptionFlags; // @[FPU.scala 722:25]
  wire [64:0] RecFNToRecFN_io_in; // @[FPU.scala 746:34]
  wire [1:0] RecFNToRecFN_io_roundingMode; // @[FPU.scala 746:34]
  wire [32:0] RecFNToRecFN_io_out; // @[FPU.scala 746:34]
  wire [4:0] RecFNToRecFN_io_exceptionFlags; // @[FPU.scala 746:34]
  reg  ex_reg_valid; // @[FPU.scala 509:25]
  wire  req_valid = ex_reg_valid | io_cp_req_valid; // @[FPU.scala 510:32]
  reg [31:0] ex_reg_inst; // @[Reg.scala 34:16]
  wire  ex_cp_valid = io_cp_req_ready & io_cp_req_valid; // @[Decoupled.scala 30:37]
  reg  mem_reg_valid; // @[FPU.scala 513:26]
  reg [31:0] mem_reg_inst; // @[Reg.scala 34:16]
  reg  mem_cp_valid; // @[FPU.scala 515:25]
  wire  killm = (io_killm | io_nack_mem) & ~mem_cp_valid; // @[FPU.scala 516:41]
  wire  _T_225 = ~killm; // @[FPU.scala 517:49]
  reg  wb_reg_valid; // @[FPU.scala 517:25]
  reg  wb_cp_valid; // @[FPU.scala 518:24]
  reg [4:0] _T_282_cmd; // @[Reg.scala 34:16]
  reg  _T_282_ldst; // @[Reg.scala 34:16]
  reg  _T_282_ren3; // @[Reg.scala 34:16]
  reg  _T_282_swap23; // @[Reg.scala 34:16]
  reg  _T_282_single; // @[Reg.scala 34:16]
  reg  _T_282_fromint; // @[Reg.scala 34:16]
  reg  _T_282_toint; // @[Reg.scala 34:16]
  reg  _T_282_fastpipe; // @[Reg.scala 34:16]
  reg  _T_282_fma; // @[Reg.scala 34:16]
  reg  _T_282_div; // @[Reg.scala 34:16]
  reg  _T_282_sqrt; // @[Reg.scala 34:16]
  reg  _T_282_wflags; // @[Reg.scala 34:16]
  wire [4:0] ex_ctrl_cmd = ex_cp_valid ? io_cp_req_bits_cmd : _T_282_cmd; // @[FPU.scala 529:20]
  wire  ex_ctrl_ldst = ex_cp_valid ? io_cp_req_bits_ldst : _T_282_ldst; // @[FPU.scala 529:20]
  wire  ex_ctrl_ren3 = ex_cp_valid ? io_cp_req_bits_ren3 : _T_282_ren3; // @[FPU.scala 529:20]
  wire  ex_ctrl_swap23 = ex_cp_valid ? io_cp_req_bits_swap23 : _T_282_swap23; // @[FPU.scala 529:20]
  wire  ex_ctrl_single = ex_cp_valid ? io_cp_req_bits_single : _T_282_single; // @[FPU.scala 529:20]
  wire  ex_ctrl_fromint = ex_cp_valid ? io_cp_req_bits_fromint : _T_282_fromint; // @[FPU.scala 529:20]
  wire  ex_ctrl_toint = ex_cp_valid ? io_cp_req_bits_toint : _T_282_toint; // @[FPU.scala 529:20]
  wire  ex_ctrl_fastpipe = ex_cp_valid ? io_cp_req_bits_fastpipe : _T_282_fastpipe; // @[FPU.scala 529:20]
  wire  ex_ctrl_fma = ex_cp_valid ? io_cp_req_bits_fma : _T_282_fma; // @[FPU.scala 529:20]
  wire  ex_ctrl_div = ex_cp_valid ? io_cp_req_bits_div : _T_282_div; // @[FPU.scala 529:20]
  wire  ex_ctrl_sqrt = ex_cp_valid ? io_cp_req_bits_sqrt : _T_282_sqrt; // @[FPU.scala 529:20]
  wire  ex_ctrl_wflags = ex_cp_valid ? io_cp_req_bits_wflags : _T_282_wflags; // @[FPU.scala 529:20]
  reg  mem_ctrl_single; // @[Reg.scala 34:16]
  reg  mem_ctrl_fromint; // @[Reg.scala 34:16]
  reg  mem_ctrl_toint; // @[Reg.scala 34:16]
  reg  mem_ctrl_fastpipe; // @[Reg.scala 34:16]
  reg  mem_ctrl_fma; // @[Reg.scala 34:16]
  reg  mem_ctrl_div; // @[Reg.scala 34:16]
  reg  mem_ctrl_sqrt; // @[Reg.scala 34:16]
  reg  mem_ctrl_wflags; // @[Reg.scala 34:16]
  reg  wb_ctrl_toint; // @[Reg.scala 34:16]
  reg  load_wb; // @[FPU.scala 534:20]
  wire  _T_383 = ~io_dmem_resp_type[0]; // @[FPU.scala 535:34]
  reg  load_wb_single; // @[Reg.scala 34:16]
  reg [63:0] load_wb_data; // @[Reg.scala 34:16]
  reg [4:0] load_wb_tag; // @[Reg.scala 34:16]
  wire  _T_391 = load_wb_data[30:23] == 8'h0; // @[recFNFromFN.scala 51:34]
  wire  _T_393 = load_wb_data[22:0] == 23'h0; // @[recFNFromFN.scala 52:38]
  wire  _T_394 = _T_391 & _T_393; // @[recFNFromFN.scala 53:34]
  wire [31:0] _T_395 = {load_wb_data[22:0], 9'h0}; // @[recFNFromFN.scala 56:26]
  wire  _T_399 = _T_395[31:16] != 16'h0; // @[CircuitMath.scala 37:22]
  wire  _T_403 = _T_395[31:24] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_407 = _T_395[31:28] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_413 = _T_395[30] ? 2'h2 : {{1'd0}, _T_395[29]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_414 = _T_395[31] ? 2'h3 : _T_413; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_420 = _T_395[26] ? 2'h2 : {{1'd0}, _T_395[25]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_421 = _T_395[27] ? 2'h3 : _T_420; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_422 = _T_407 ? _T_414 : _T_421; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_423 = {_T_407,_T_422}; // @[Cat.scala 30:58]
  wire  _T_427 = _T_395[23:20] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_433 = _T_395[22] ? 2'h2 : {{1'd0}, _T_395[21]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_434 = _T_395[23] ? 2'h3 : _T_433; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_440 = _T_395[18] ? 2'h2 : {{1'd0}, _T_395[17]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_441 = _T_395[19] ? 2'h3 : _T_440; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_442 = _T_427 ? _T_434 : _T_441; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_443 = {_T_427,_T_442}; // @[Cat.scala 30:58]
  wire [2:0] _T_444 = _T_403 ? _T_423 : _T_443; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_445 = {_T_403,_T_444}; // @[Cat.scala 30:58]
  wire  _T_449 = _T_395[15:8] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_453 = _T_395[15:12] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_459 = _T_395[14] ? 2'h2 : {{1'd0}, _T_395[13]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_460 = _T_395[15] ? 2'h3 : _T_459; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_466 = _T_395[10] ? 2'h2 : {{1'd0}, _T_395[9]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_467 = _T_395[11] ? 2'h3 : _T_466; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_468 = _T_453 ? _T_460 : _T_467; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_469 = {_T_453,_T_468}; // @[Cat.scala 30:58]
  wire  _T_473 = _T_395[7:4] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_479 = _T_395[6] ? 2'h2 : {{1'd0}, _T_395[5]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_480 = _T_395[7] ? 2'h3 : _T_479; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_486 = _T_395[2] ? 2'h2 : {{1'd0}, _T_395[1]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_487 = _T_395[3] ? 2'h3 : _T_486; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_488 = _T_473 ? _T_480 : _T_487; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_489 = {_T_473,_T_488}; // @[Cat.scala 30:58]
  wire [2:0] _T_490 = _T_449 ? _T_469 : _T_489; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_491 = {_T_449,_T_490}; // @[Cat.scala 30:58]
  wire [3:0] _T_492 = _T_399 ? _T_445 : _T_491; // @[CircuitMath.scala 38:21]
  wire [4:0] _T_493 = {_T_399,_T_492}; // @[Cat.scala 30:58]
  wire [4:0] _T_494 = ~_T_493; // @[recFNFromFN.scala 56:13]
  wire [53:0] _GEN_149 = {{31'd0}, load_wb_data[22:0]}; // @[recFNFromFN.scala 58:25]
  wire [53:0] _T_495 = _GEN_149 << _T_494; // @[recFNFromFN.scala 58:25]
  wire [22:0] _T_498 = {_T_495[21:0],1'h0}; // @[Cat.scala 30:58]
  wire [8:0] _GEN_150 = {{4'd0}, _T_494}; // @[recFNFromFN.scala 62:27]
  wire [8:0] _T_504 = _GEN_150 ^ 9'h1ff; // @[recFNFromFN.scala 62:27]
  wire [8:0] _T_505 = _T_391 ? _T_504 : {{1'd0}, load_wb_data[30:23]}; // @[recFNFromFN.scala 61:16]
  wire [1:0] _T_509 = _T_391 ? 2'h2 : 2'h1; // @[recFNFromFN.scala 64:47]
  wire [7:0] _GEN_151 = {{6'd0}, _T_509}; // @[recFNFromFN.scala 64:42]
  wire [7:0] _T_510 = 8'h80 | _GEN_151; // @[recFNFromFN.scala 64:42]
  wire [8:0] _GEN_152 = {{1'd0}, _T_510}; // @[recFNFromFN.scala 64:15]
  wire [8:0] _T_512 = _T_505 + _GEN_152; // @[recFNFromFN.scala 64:15]
  wire  _T_517 = ~_T_393; // @[recFNFromFN.scala 68:17]
  wire  _T_518 = _T_512[8:7] == 2'h3 & _T_517; // @[recFNFromFN.scala 67:63]
  wire [2:0] _T_522 = _T_394 ? 3'h7 : 3'h0; // @[Bitwise.scala 71:12]
  wire [8:0] _T_523 = {_T_522, 6'h0}; // @[recFNFromFN.scala 71:45]
  wire [8:0] _T_524 = ~_T_523; // @[recFNFromFN.scala 71:28]
  wire [8:0] _T_525 = _T_512 & _T_524; // @[recFNFromFN.scala 71:26]
  wire [6:0] _T_526 = {_T_518, 6'h0}; // @[recFNFromFN.scala 72:22]
  wire [8:0] _GEN_153 = {{2'd0}, _T_526}; // @[recFNFromFN.scala 71:64]
  wire [8:0] _T_527 = _T_525 | _GEN_153; // @[recFNFromFN.scala 71:64]
  wire [22:0] _T_528 = _T_391 ? _T_498 : load_wb_data[22:0]; // @[recFNFromFN.scala 73:27]
  wire [32:0] rec_s = {load_wb_data[31],_T_527,_T_528}; // @[Cat.scala 30:58]
  wire  _T_534 = load_wb_data[62:52] == 11'h0; // @[recFNFromFN.scala 51:34]
  wire  _T_536 = load_wb_data[51:0] == 52'h0; // @[recFNFromFN.scala 52:38]
  wire  _T_537 = _T_534 & _T_536; // @[recFNFromFN.scala 53:34]
  wire [63:0] _T_538 = {load_wb_data[51:0], 12'h0}; // @[recFNFromFN.scala 56:26]
  wire  _T_542 = _T_538[63:32] != 32'h0; // @[CircuitMath.scala 37:22]
  wire  _T_546 = _T_538[63:48] != 16'h0; // @[CircuitMath.scala 37:22]
  wire  _T_550 = _T_538[63:56] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_554 = _T_538[63:60] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_560 = _T_538[62] ? 2'h2 : {{1'd0}, _T_538[61]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_561 = _T_538[63] ? 2'h3 : _T_560; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_567 = _T_538[58] ? 2'h2 : {{1'd0}, _T_538[57]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_568 = _T_538[59] ? 2'h3 : _T_567; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_569 = _T_554 ? _T_561 : _T_568; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_570 = {_T_554,_T_569}; // @[Cat.scala 30:58]
  wire  _T_574 = _T_538[55:52] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_580 = _T_538[54] ? 2'h2 : {{1'd0}, _T_538[53]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_581 = _T_538[55] ? 2'h3 : _T_580; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_587 = _T_538[50] ? 2'h2 : {{1'd0}, _T_538[49]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_588 = _T_538[51] ? 2'h3 : _T_587; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_589 = _T_574 ? _T_581 : _T_588; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_590 = {_T_574,_T_589}; // @[Cat.scala 30:58]
  wire [2:0] _T_591 = _T_550 ? _T_570 : _T_590; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_592 = {_T_550,_T_591}; // @[Cat.scala 30:58]
  wire  _T_596 = _T_538[47:40] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_600 = _T_538[47:44] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_606 = _T_538[46] ? 2'h2 : {{1'd0}, _T_538[45]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_607 = _T_538[47] ? 2'h3 : _T_606; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_613 = _T_538[42] ? 2'h2 : {{1'd0}, _T_538[41]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_614 = _T_538[43] ? 2'h3 : _T_613; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_615 = _T_600 ? _T_607 : _T_614; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_616 = {_T_600,_T_615}; // @[Cat.scala 30:58]
  wire  _T_620 = _T_538[39:36] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_626 = _T_538[38] ? 2'h2 : {{1'd0}, _T_538[37]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_627 = _T_538[39] ? 2'h3 : _T_626; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_633 = _T_538[34] ? 2'h2 : {{1'd0}, _T_538[33]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_634 = _T_538[35] ? 2'h3 : _T_633; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_635 = _T_620 ? _T_627 : _T_634; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_636 = {_T_620,_T_635}; // @[Cat.scala 30:58]
  wire [2:0] _T_637 = _T_596 ? _T_616 : _T_636; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_638 = {_T_596,_T_637}; // @[Cat.scala 30:58]
  wire [3:0] _T_639 = _T_546 ? _T_592 : _T_638; // @[CircuitMath.scala 38:21]
  wire [4:0] _T_640 = {_T_546,_T_639}; // @[Cat.scala 30:58]
  wire  _T_644 = _T_538[31:16] != 16'h0; // @[CircuitMath.scala 37:22]
  wire  _T_648 = _T_538[31:24] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_652 = _T_538[31:28] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_658 = _T_538[30] ? 2'h2 : {{1'd0}, _T_538[29]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_659 = _T_538[31] ? 2'h3 : _T_658; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_665 = _T_538[26] ? 2'h2 : {{1'd0}, _T_538[25]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_666 = _T_538[27] ? 2'h3 : _T_665; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_667 = _T_652 ? _T_659 : _T_666; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_668 = {_T_652,_T_667}; // @[Cat.scala 30:58]
  wire  _T_672 = _T_538[23:20] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_678 = _T_538[22] ? 2'h2 : {{1'd0}, _T_538[21]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_679 = _T_538[23] ? 2'h3 : _T_678; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_685 = _T_538[18] ? 2'h2 : {{1'd0}, _T_538[17]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_686 = _T_538[19] ? 2'h3 : _T_685; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_687 = _T_672 ? _T_679 : _T_686; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_688 = {_T_672,_T_687}; // @[Cat.scala 30:58]
  wire [2:0] _T_689 = _T_648 ? _T_668 : _T_688; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_690 = {_T_648,_T_689}; // @[Cat.scala 30:58]
  wire  _T_694 = _T_538[15:8] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_698 = _T_538[15:12] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_704 = _T_538[14] ? 2'h2 : {{1'd0}, _T_538[13]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_705 = _T_538[15] ? 2'h3 : _T_704; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_711 = _T_538[10] ? 2'h2 : {{1'd0}, _T_538[9]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_712 = _T_538[11] ? 2'h3 : _T_711; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_713 = _T_698 ? _T_705 : _T_712; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_714 = {_T_698,_T_713}; // @[Cat.scala 30:58]
  wire  _T_718 = _T_538[7:4] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_724 = _T_538[6] ? 2'h2 : {{1'd0}, _T_538[5]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_725 = _T_538[7] ? 2'h3 : _T_724; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_731 = _T_538[2] ? 2'h2 : {{1'd0}, _T_538[1]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_732 = _T_538[3] ? 2'h3 : _T_731; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_733 = _T_718 ? _T_725 : _T_732; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_734 = {_T_718,_T_733}; // @[Cat.scala 30:58]
  wire [2:0] _T_735 = _T_694 ? _T_714 : _T_734; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_736 = {_T_694,_T_735}; // @[Cat.scala 30:58]
  wire [3:0] _T_737 = _T_644 ? _T_690 : _T_736; // @[CircuitMath.scala 38:21]
  wire [4:0] _T_738 = {_T_644,_T_737}; // @[Cat.scala 30:58]
  wire [4:0] _T_739 = _T_542 ? _T_640 : _T_738; // @[CircuitMath.scala 38:21]
  wire [5:0] _T_740 = {_T_542,_T_739}; // @[Cat.scala 30:58]
  wire [5:0] _T_741 = ~_T_740; // @[recFNFromFN.scala 56:13]
  wire [114:0] _GEN_154 = {{63'd0}, load_wb_data[51:0]}; // @[recFNFromFN.scala 58:25]
  wire [114:0] _T_742 = _GEN_154 << _T_741; // @[recFNFromFN.scala 58:25]
  wire [51:0] _T_745 = {_T_742[50:0],1'h0}; // @[Cat.scala 30:58]
  wire [11:0] _GEN_155 = {{6'd0}, _T_741}; // @[recFNFromFN.scala 62:27]
  wire [11:0] _T_751 = _GEN_155 ^ 12'hfff; // @[recFNFromFN.scala 62:27]
  wire [11:0] _T_752 = _T_534 ? _T_751 : {{1'd0}, load_wb_data[62:52]}; // @[recFNFromFN.scala 61:16]
  wire [1:0] _T_756 = _T_534 ? 2'h2 : 2'h1; // @[recFNFromFN.scala 64:47]
  wire [10:0] _GEN_156 = {{9'd0}, _T_756}; // @[recFNFromFN.scala 64:42]
  wire [10:0] _T_757 = 11'h400 | _GEN_156; // @[recFNFromFN.scala 64:42]
  wire [11:0] _GEN_157 = {{1'd0}, _T_757}; // @[recFNFromFN.scala 64:15]
  wire [11:0] _T_759 = _T_752 + _GEN_157; // @[recFNFromFN.scala 64:15]
  wire  _T_764 = ~_T_536; // @[recFNFromFN.scala 68:17]
  wire  _T_765 = _T_759[11:10] == 2'h3 & _T_764; // @[recFNFromFN.scala 67:63]
  wire [2:0] _T_769 = _T_537 ? 3'h7 : 3'h0; // @[Bitwise.scala 71:12]
  wire [11:0] _T_770 = {_T_769, 9'h0}; // @[recFNFromFN.scala 71:45]
  wire [11:0] _T_771 = ~_T_770; // @[recFNFromFN.scala 71:28]
  wire [11:0] _T_772 = _T_759 & _T_771; // @[recFNFromFN.scala 71:26]
  wire [9:0] _T_773 = {_T_765, 9'h0}; // @[recFNFromFN.scala 72:22]
  wire [11:0] _GEN_158 = {{2'd0}, _T_773}; // @[recFNFromFN.scala 71:64]
  wire [11:0] _T_774 = _T_772 | _GEN_158; // @[recFNFromFN.scala 71:64]
  wire [51:0] _T_775 = _T_534 ? _T_745 : load_wb_data[51:0]; // @[recFNFromFN.scala 73:27]
  wire [64:0] _T_777 = {load_wb_data[63],_T_774,_T_775}; // @[Cat.scala 30:58]
  wire [64:0] _GEN_159 = {{32'd0}, rec_s}; // @[FPU.scala 543:33]
  wire [64:0] _T_779 = _GEN_159 | 65'he004000000000000; // @[FPU.scala 543:33]
  reg [4:0] ex_ra1; // @[FPU.scala 554:53]
  reg [4:0] ex_ra2; // @[FPU.scala 554:53]
  reg [4:0] ex_ra3; // @[FPU.scala 554:53]
  wire  _T_787 = ~fp_decoder_io_sigs_swap12; // @[FPU.scala 557:13]
  wire [4:0] _GEN_58 = ~fp_decoder_io_sigs_swap12 ? io_inst[19:15] : ex_ra1; // @[FPU.scala 557:30 FPU.scala 557:39 FPU.scala 554:53]
  wire [4:0] _GEN_59 = fp_decoder_io_sigs_swap12 ? io_inst[19:15] : ex_ra2; // @[FPU.scala 558:29 FPU.scala 558:38 FPU.scala 554:53]
  wire [4:0] _GEN_60 = fp_decoder_io_sigs_ren1 ? _GEN_58 : ex_ra1; // @[FPU.scala 556:25 FPU.scala 554:53]
  wire [4:0] _GEN_61 = fp_decoder_io_sigs_ren1 ? _GEN_59 : ex_ra2; // @[FPU.scala 556:25 FPU.scala 554:53]
  wire [2:0] ex_rm = ex_reg_inst[14:12] == 3'h7 ? io_fcsr_rm : ex_reg_inst[14:12]; // @[FPU.scala 567:18]
  wire [64:0] _GEN_72 = io_cp_req_bits_swap23 ? io_cp_req_bits_in3 : io_cp_req_bits_in2; // @[FPU.scala 578:34 FPU.scala 579:15 FPU.scala 577:9]
  wire [64:0] _GEN_73 = io_cp_req_bits_swap23 ? io_cp_req_bits_in2 : io_cp_req_bits_in3; // @[FPU.scala 578:34 FPU.scala 580:15 FPU.scala 577:9]
  wire  _T_859 = req_valid & ex_ctrl_fma; // @[FPU.scala 585:33]
  wire [4:0] _T_865 = ex_ctrl_cmd & 5'hd; // @[FPU.scala 589:97]
  wire  _T_870 = fpiu_io_out_valid & mem_cp_valid & mem_ctrl_toint; // @[FPU.scala 593:42]
  wire [63:0] _GEN_95 = fpiu_io_out_valid & mem_cp_valid & mem_ctrl_toint ? fpiu_io_out_bits_toint : 64'h0; // @[FPU.scala 593:60 FPU.scala 594:26 FPU.scala 526:24]
  reg  divSqrt_wen; // @[FPU.scala 608:24]
  reg [4:0] divSqrt_waddr; // @[FPU.scala 610:26]
  reg  divSqrt_single; // @[FPU.scala 611:27]
  reg  divSqrt_in_flight; // @[FPU.scala 614:30]
  reg  divSqrt_killed; // @[FPU.scala 615:27]
  wire  _T_885 = ~ex_ctrl_single; // @[FPU.scala 625:59]
  wire  _T_893 = mem_ctrl_fma & mem_ctrl_single; // @[FPU.scala 622:56]
  wire [1:0] _T_896 = _T_893 ? 2'h2 : 2'h0; // @[FPU.scala 631:23]
  wire  _T_899 = mem_ctrl_fma & ~mem_ctrl_single; // @[FPU.scala 627:62]
  wire [2:0] _T_902 = _T_899 ? 3'h4 : 3'h0; // @[FPU.scala 631:23]
  wire  _T_903 = mem_ctrl_fastpipe | mem_ctrl_fromint; // @[FPU.scala 631:78]
  wire [1:0] _GEN_160 = {{1'd0}, _T_903}; // @[FPU.scala 631:78]
  wire [1:0] _T_904 = _GEN_160 | _T_896; // @[FPU.scala 631:78]
  wire [2:0] _GEN_161 = {{1'd0}, _T_904}; // @[FPU.scala 631:78]
  wire [2:0] memLatencyMask = _GEN_161 | _T_902; // @[FPU.scala 631:78]
  reg [2:0] wen; // @[FPU.scala 645:16]
  reg [4:0] wbInfo_0_rd; // @[FPU.scala 646:19]
  reg  wbInfo_0_single; // @[FPU.scala 646:19]
  reg  wbInfo_0_cp; // @[FPU.scala 646:19]
  reg [1:0] wbInfo_0_pipeid; // @[FPU.scala 646:19]
  reg [4:0] wbInfo_1_rd; // @[FPU.scala 646:19]
  reg  wbInfo_1_single; // @[FPU.scala 646:19]
  reg  wbInfo_1_cp; // @[FPU.scala 646:19]
  reg [1:0] wbInfo_1_pipeid; // @[FPU.scala 646:19]
  reg [4:0] wbInfo_2_rd; // @[FPU.scala 646:19]
  reg  wbInfo_2_single; // @[FPU.scala 646:19]
  reg  wbInfo_2_cp; // @[FPU.scala 646:19]
  reg [1:0] wbInfo_2_pipeid; // @[FPU.scala 646:19]
  wire  mem_wen = mem_reg_valid & (mem_ctrl_fma | mem_ctrl_fastpipe | mem_ctrl_fromint); // @[FPU.scala 647:31]
  wire [1:0] _T_970 = ex_ctrl_fastpipe ? 2'h2 : 2'h0; // @[FPU.scala 631:23]
  wire [1:0] _T_973 = ex_ctrl_fromint ? 2'h2 : 2'h0; // @[FPU.scala 631:23]
  wire  _T_974 = ex_ctrl_fma & ex_ctrl_single; // @[FPU.scala 622:56]
  wire [2:0] _T_977 = _T_974 ? 3'h4 : 3'h0; // @[FPU.scala 631:23]
  wire  _T_980 = ex_ctrl_fma & _T_885; // @[FPU.scala 627:62]
  wire [3:0] _T_983 = _T_980 ? 4'h8 : 4'h0; // @[FPU.scala 631:23]
  wire [1:0] _T_984 = _T_970 | _T_973; // @[FPU.scala 631:78]
  wire [2:0] _GEN_162 = {{1'd0}, _T_984}; // @[FPU.scala 631:78]
  wire [2:0] _T_985 = _GEN_162 | _T_977; // @[FPU.scala 631:78]
  wire [3:0] _GEN_163 = {{1'd0}, _T_985}; // @[FPU.scala 631:78]
  wire [3:0] _T_986 = _GEN_163 | _T_983; // @[FPU.scala 631:78]
  wire [3:0] _GEN_164 = {{1'd0}, memLatencyMask}; // @[FPU.scala 648:62]
  wire [3:0] _T_987 = _GEN_164 & _T_986; // @[FPU.scala 648:62]
  wire [2:0] _T_993 = ex_ctrl_fastpipe ? 3'h4 : 3'h0; // @[FPU.scala 631:23]
  wire [2:0] _T_996 = ex_ctrl_fromint ? 3'h4 : 3'h0; // @[FPU.scala 631:23]
  wire [3:0] _T_1000 = _T_974 ? 4'h8 : 4'h0; // @[FPU.scala 631:23]
  wire [4:0] _T_1006 = _T_980 ? 5'h10 : 5'h0; // @[FPU.scala 631:23]
  wire [2:0] _T_1007 = _T_993 | _T_996; // @[FPU.scala 631:78]
  wire [3:0] _GEN_165 = {{1'd0}, _T_1007}; // @[FPU.scala 631:78]
  wire [3:0] _T_1008 = _GEN_165 | _T_1000; // @[FPU.scala 631:78]
  wire [4:0] _GEN_166 = {{1'd0}, _T_1008}; // @[FPU.scala 631:78]
  wire [4:0] _T_1009 = _GEN_166 | _T_1006; // @[FPU.scala 631:78]
  wire [4:0] _GEN_167 = {{2'd0}, wen}; // @[FPU.scala 648:101]
  wire [4:0] _T_1010 = _GEN_167 & _T_1009; // @[FPU.scala 648:101]
  wire  _T_1013 = mem_wen & _T_987 != 4'h0 | _T_1010 != 5'h0; // @[FPU.scala 648:93]
  reg  write_port_busy; // @[Reg.scala 34:16]
  wire [4:0] _GEN_98 = wen[1] ? wbInfo_1_rd : wbInfo_0_rd; // @[FPU.scala 651:21 FPU.scala 651:33 FPU.scala 646:19]
  wire  _GEN_99 = wen[1] ? wbInfo_1_single : wbInfo_0_single; // @[FPU.scala 651:21 FPU.scala 651:33 FPU.scala 646:19]
  wire  _GEN_100 = wen[1] ? wbInfo_1_cp : wbInfo_0_cp; // @[FPU.scala 651:21 FPU.scala 651:33 FPU.scala 646:19]
  wire [1:0] _GEN_101 = wen[1] ? wbInfo_1_pipeid : wbInfo_0_pipeid; // @[FPU.scala 651:21 FPU.scala 651:33 FPU.scala 646:19]
  wire [4:0] _GEN_102 = wen[2] ? wbInfo_2_rd : wbInfo_1_rd; // @[FPU.scala 651:21 FPU.scala 651:33 FPU.scala 646:19]
  wire  _GEN_103 = wen[2] ? wbInfo_2_single : wbInfo_1_single; // @[FPU.scala 651:21 FPU.scala 651:33 FPU.scala 646:19]
  wire  _GEN_104 = wen[2] ? wbInfo_2_cp : wbInfo_1_cp; // @[FPU.scala 651:21 FPU.scala 651:33 FPU.scala 646:19]
  wire [1:0] _GEN_105 = wen[2] ? wbInfo_2_pipeid : wbInfo_1_pipeid; // @[FPU.scala 651:21 FPU.scala 651:33 FPU.scala 646:19]
  wire [2:0] _GEN_168 = {{1'd0}, wen[2:1]}; // @[FPU.scala 656:23]
  wire [2:0] _T_1021 = _GEN_168 | memLatencyMask; // @[FPU.scala 656:23]
  wire [1:0] _T_1041 = _T_899 ? 2'h3 : 2'h0; // @[FPU.scala 633:63]
  wire [1:0] _GEN_169 = {{1'd0}, mem_ctrl_fromint}; // @[FPU.scala 633:108]
  wire [1:0] _T_1043 = _GEN_169 | _T_896; // @[FPU.scala 633:108]
  wire [1:0] _T_1044 = _T_1043 | _T_1041; // @[FPU.scala 633:108]
  wire [1:0] _T_1095 = wbInfo_0_pipeid & 2'h1; // @[Package.scala 18:26]
  wire [64:0] _T_1102 = _T_1095 >= 2'h1 ? FPUFMAPipe_io_out_bits_data : sfma_io_out_bits_data; // @[Package.scala 19:12]
  wire [64:0] _T_1107 = _T_1095 >= 2'h1 ? ifpu_io_out_bits_data : fpmu_io_out_bits_data; // @[Package.scala 19:12]
  wire [64:0] _T_1108 = wbInfo_0_pipeid >= 2'h2 ? _T_1102 : _T_1107; // @[Package.scala 19:12]
  reg [64:0] _T_1207; // @[FPU.scala 720:35]
  wire [64:0] divSqrt_wdata = divSqrt_single ? {{32'd0}, RecFNToRecFN_io_out} : _T_1207; // @[FPU.scala 749:25]
  wire [64:0] wdata0 = divSqrt_wen ? divSqrt_wdata : _T_1108; // @[FPU.scala 669:19]
  wire  wsingle = divSqrt_wen ? divSqrt_single : wbInfo_0_single; // @[FPU.scala 670:20]
  wire [64:0] _GEN_172 = {{32'd0}, wdata0[32:0]}; // @[FPU.scala 673:44]
  wire [64:0] _T_1111 = _GEN_172 | 65'he004000000000000; // @[FPU.scala 673:44]
  wire [64:0] wdata = wsingle ? _T_1111 : wdata0; // @[FPU.scala 673:19]
  wire [4:0] _T_1120 = _T_1095 >= 2'h1 ? FPUFMAPipe_io_out_bits_exc : sfma_io_out_bits_exc; // @[Package.scala 19:12]
  wire [4:0] _T_1125 = _T_1095 >= 2'h1 ? ifpu_io_out_bits_exc : fpmu_io_out_bits_exc; // @[Package.scala 19:12]
  wire [4:0] wexc = wbInfo_0_pipeid >= 2'h2 ? _T_1120 : _T_1125; // @[Package.scala 19:12]
  wire  _T_1129 = ~wbInfo_0_cp & wen[0]; // @[FPU.scala 676:24]
  wire  wb_toint_valid = wb_reg_valid & wb_ctrl_toint; // @[FPU.scala 695:37]
  reg [4:0] wb_toint_exc; // @[Reg.scala 34:16]
  wire [4:0] _T_1142 = wb_toint_valid ? wb_toint_exc : 5'h0; // @[FPU.scala 699:8]
  reg [4:0] _T_1205; // @[FPU.scala 719:35]
  wire [4:0] _T_1223 = divSqrt_single ? RecFNToRecFN_io_exceptionFlags : 5'h0; // @[FPU.scala 750:48]
  wire [4:0] divSqrt_flags = _T_1205 | _T_1223; // @[FPU.scala 750:43]
  wire [4:0] _T_1144 = divSqrt_wen ? divSqrt_flags : 5'h0; // @[FPU.scala 700:8]
  wire [4:0] _T_1145 = _T_1142 | _T_1144; // @[FPU.scala 699:48]
  wire [4:0] _T_1148 = wen[0] ? wexc : 5'h0; // @[FPU.scala 701:8]
  wire  _T_1151 = mem_reg_valid & (mem_ctrl_div | mem_ctrl_sqrt); // @[FPU.scala 703:34]
  wire  divSqrt_inReady = DivSqrtRecF64_io_sqrtOp ? DivSqrtRecF64_io_inReady_sqrt : DivSqrtRecF64_io_inReady_div; // @[FPU.scala 723:27]
  wire  _T_1155 = wen != 3'h0; // @[FPU.scala 703:97]
  wire  units_busy = mem_reg_valid & (mem_ctrl_div | mem_ctrl_sqrt) & (~divSqrt_inReady | wen != 3'h0); // @[FPU.scala 703:69]
  wire  _T_1171 = ~wb_cp_valid; // @[FPU.scala 708:36]
  reg  _T_1180; // @[FPU.scala 708:55]
  reg [1:0] _T_1203; // @[FPU.scala 718:25]
  wire  _T_1209 = DivSqrtRecF64_io_outValid_div | DivSqrtRecF64_io_outValid_sqrt; // @[FPU.scala 724:52]
  wire  _GEN_140 = DivSqrtRecF64_io_inValid & divSqrt_inReady | divSqrt_in_flight; // @[FPU.scala 731:50 FPU.scala 732:25 FPU.scala 614:30]
  FPUDecoder fp_decoder ( // @[FPU.scala 520:26]
    .io_inst(fp_decoder_io_inst),
    .io_sigs_cmd(fp_decoder_io_sigs_cmd),
    .io_sigs_ldst(fp_decoder_io_sigs_ldst),
    .io_sigs_wen(fp_decoder_io_sigs_wen),
    .io_sigs_ren1(fp_decoder_io_sigs_ren1),
    .io_sigs_ren2(fp_decoder_io_sigs_ren2),
    .io_sigs_ren3(fp_decoder_io_sigs_ren3),
    .io_sigs_swap12(fp_decoder_io_sigs_swap12),
    .io_sigs_swap23(fp_decoder_io_sigs_swap23),
    .io_sigs_single(fp_decoder_io_sigs_single),
    .io_sigs_fromint(fp_decoder_io_sigs_fromint),
    .io_sigs_toint(fp_decoder_io_sigs_toint),
    .io_sigs_fastpipe(fp_decoder_io_sigs_fastpipe),
    .io_sigs_fma(fp_decoder_io_sigs_fma),
    .io_sigs_div(fp_decoder_io_sigs_div),
    .io_sigs_sqrt(fp_decoder_io_sigs_sqrt),
    .io_sigs_wflags(fp_decoder_io_sigs_wflags)
  );
  FPUFMAPipe sfma ( // @[FPU.scala 584:20]
    .clock(sfma_clock),
    .reset(sfma_reset),
    .io_in_valid(sfma_io_in_valid),
    .io_in_bits_cmd(sfma_io_in_bits_cmd),
    .io_in_bits_ren3(sfma_io_in_bits_ren3),
    .io_in_bits_swap23(sfma_io_in_bits_swap23),
    .io_in_bits_rm(sfma_io_in_bits_rm),
    .io_in_bits_in1(sfma_io_in_bits_in1),
    .io_in_bits_in2(sfma_io_in_bits_in2),
    .io_in_bits_in3(sfma_io_in_bits_in3),
    .io_out_bits_data(sfma_io_out_bits_data),
    .io_out_bits_exc(sfma_io_out_bits_exc)
  );
  FPToInt fpiu ( // @[FPU.scala 588:20]
    .clock(fpiu_clock),
    .io_in_valid(fpiu_io_in_valid),
    .io_in_bits_cmd(fpiu_io_in_bits_cmd),
    .io_in_bits_ldst(fpiu_io_in_bits_ldst),
    .io_in_bits_single(fpiu_io_in_bits_single),
    .io_in_bits_rm(fpiu_io_in_bits_rm),
    .io_in_bits_typ(fpiu_io_in_bits_typ),
    .io_in_bits_in1(fpiu_io_in_bits_in1),
    .io_in_bits_in2(fpiu_io_in_bits_in2),
    .io_as_double_rm(fpiu_io_as_double_rm),
    .io_as_double_in1(fpiu_io_as_double_in1),
    .io_as_double_in2(fpiu_io_as_double_in2),
    .io_out_valid(fpiu_io_out_valid),
    .io_out_bits_lt(fpiu_io_out_bits_lt),
    .io_out_bits_store(fpiu_io_out_bits_store),
    .io_out_bits_toint(fpiu_io_out_bits_toint),
    .io_out_bits_exc(fpiu_io_out_bits_exc)
  );
  IntToFP ifpu ( // @[FPU.scala 598:20]
    .clock(ifpu_clock),
    .reset(ifpu_reset),
    .io_in_valid(ifpu_io_in_valid),
    .io_in_bits_cmd(ifpu_io_in_bits_cmd),
    .io_in_bits_single(ifpu_io_in_bits_single),
    .io_in_bits_rm(ifpu_io_in_bits_rm),
    .io_in_bits_typ(ifpu_io_in_bits_typ),
    .io_in_bits_in1(ifpu_io_in_bits_in1),
    .io_out_bits_data(ifpu_io_out_bits_data),
    .io_out_bits_exc(ifpu_io_out_bits_exc)
  );
  FPToFP fpmu ( // @[FPU.scala 603:20]
    .clock(fpmu_clock),
    .reset(fpmu_reset),
    .io_in_valid(fpmu_io_in_valid),
    .io_in_bits_cmd(fpmu_io_in_bits_cmd),
    .io_in_bits_single(fpmu_io_in_bits_single),
    .io_in_bits_rm(fpmu_io_in_bits_rm),
    .io_in_bits_in1(fpmu_io_in_bits_in1),
    .io_in_bits_in2(fpmu_io_in_bits_in2),
    .io_out_bits_data(fpmu_io_out_bits_data),
    .io_out_bits_exc(fpmu_io_out_bits_exc),
    .io_lt(fpmu_io_lt)
  );
  FPUFMAPipe_1 FPUFMAPipe ( // @[FPU.scala 624:28]
    .clock(FPUFMAPipe_clock),
    .reset(FPUFMAPipe_reset),
    .io_in_valid(FPUFMAPipe_io_in_valid),
    .io_in_bits_cmd(FPUFMAPipe_io_in_bits_cmd),
    .io_in_bits_ren3(FPUFMAPipe_io_in_bits_ren3),
    .io_in_bits_swap23(FPUFMAPipe_io_in_bits_swap23),
    .io_in_bits_rm(FPUFMAPipe_io_in_bits_rm),
    .io_in_bits_in1(FPUFMAPipe_io_in_bits_in1),
    .io_in_bits_in2(FPUFMAPipe_io_in_bits_in2),
    .io_in_bits_in3(FPUFMAPipe_io_in_bits_in3),
    .io_out_bits_data(FPUFMAPipe_io_out_bits_data),
    .io_out_bits_exc(FPUFMAPipe_io_out_bits_exc)
  );
  DivSqrtRecF64 DivSqrtRecF64 ( // @[FPU.scala 722:25]
    .clock(DivSqrtRecF64_clock),
    .reset(DivSqrtRecF64_reset),
    .io_inReady_div(DivSqrtRecF64_io_inReady_div),
    .io_inReady_sqrt(DivSqrtRecF64_io_inReady_sqrt),
    .io_inValid(DivSqrtRecF64_io_inValid),
    .io_sqrtOp(DivSqrtRecF64_io_sqrtOp),
    .io_a(DivSqrtRecF64_io_a),
    .io_b(DivSqrtRecF64_io_b),
    .io_roundingMode(DivSqrtRecF64_io_roundingMode),
    .io_outValid_div(DivSqrtRecF64_io_outValid_div),
    .io_outValid_sqrt(DivSqrtRecF64_io_outValid_sqrt),
    .io_out(DivSqrtRecF64_io_out),
    .io_exceptionFlags(DivSqrtRecF64_io_exceptionFlags)
  );
  RecFNToRecFN_2 RecFNToRecFN ( // @[FPU.scala 746:34]
    .io_in(RecFNToRecFN_io_in),
    .io_roundingMode(RecFNToRecFN_io_roundingMode),
    .io_out(RecFNToRecFN_io_out),
    .io_exceptionFlags(RecFNToRecFN_io_exceptionFlags)
  );
  assign regfile__T_849_addr = ex_ra1;
  assign regfile__T_849_data = regfile[regfile__T_849_addr]; // @[FPU.scala 547:20]
  assign regfile__T_853_addr = ex_ra2;
  assign regfile__T_853_data = regfile[regfile__T_853_addr]; // @[FPU.scala 547:20]
  assign regfile__T_857_addr = ex_ra3;
  assign regfile__T_857_data = regfile[regfile__T_857_addr]; // @[FPU.scala 547:20]
  assign regfile__T_782_data = load_wb_single ? _T_779 : _T_777;
  assign regfile__T_782_addr = load_wb_tag;
  assign regfile__T_782_mask = 1'h1;
  assign regfile__T_782_en = load_wb;
  assign regfile__T_1131_data = wsingle ? _T_1111 : wdata0;
  assign regfile__T_1131_addr = divSqrt_wen ? divSqrt_waddr : wbInfo_0_rd;
  assign regfile__T_1131_mask = 1'h1;
  assign regfile__T_1131_en = _T_1129 | divSqrt_wen;
  assign io_fcsr_flags_valid = wb_toint_valid | divSqrt_wen | wen[0]; // @[FPU.scala 697:56]
  assign io_fcsr_flags_bits = _T_1145 | _T_1148; // @[FPU.scala 700:46]
  assign io_store_data = fpiu_io_out_bits_store; // @[FPU.scala 591:17]
  assign io_toint_data = fpiu_io_out_bits_toint; // @[FPU.scala 592:17]
  assign io_fcsr_rdy = ~(ex_reg_valid & ex_ctrl_wflags | mem_reg_valid & mem_ctrl_wflags | wb_toint_valid | _T_1155 |
    divSqrt_in_flight); // @[FPU.scala 704:18]
  assign io_nack_mem = units_busy | write_port_busy | divSqrt_in_flight; // @[FPU.scala 705:48]
  assign io_illegal_rm = io_inst[14] & (io_inst[13:12] < 2'h3 | io_fcsr_rm >= 3'h4); // @[FPU.scala 712:32]
  assign io_dec_cmd = fp_decoder_io_sigs_cmd; // @[FPU.scala 706:10]
  assign io_dec_ldst = fp_decoder_io_sigs_ldst; // @[FPU.scala 706:10]
  assign io_dec_wen = fp_decoder_io_sigs_wen; // @[FPU.scala 706:10]
  assign io_dec_ren1 = fp_decoder_io_sigs_ren1; // @[FPU.scala 706:10]
  assign io_dec_ren2 = fp_decoder_io_sigs_ren2; // @[FPU.scala 706:10]
  assign io_dec_ren3 = fp_decoder_io_sigs_ren3; // @[FPU.scala 706:10]
  assign io_dec_swap12 = fp_decoder_io_sigs_swap12; // @[FPU.scala 706:10]
  assign io_dec_swap23 = fp_decoder_io_sigs_swap23; // @[FPU.scala 706:10]
  assign io_dec_single = fp_decoder_io_sigs_single; // @[FPU.scala 706:10]
  assign io_dec_fromint = fp_decoder_io_sigs_fromint; // @[FPU.scala 706:10]
  assign io_dec_toint = fp_decoder_io_sigs_toint; // @[FPU.scala 706:10]
  assign io_dec_fastpipe = fp_decoder_io_sigs_fastpipe; // @[FPU.scala 706:10]
  assign io_dec_fma = fp_decoder_io_sigs_fma; // @[FPU.scala 706:10]
  assign io_dec_div = fp_decoder_io_sigs_div; // @[FPU.scala 706:10]
  assign io_dec_sqrt = fp_decoder_io_sigs_sqrt; // @[FPU.scala 706:10]
  assign io_dec_wflags = fp_decoder_io_sigs_wflags; // @[FPU.scala 706:10]
  assign io_sboard_set = wb_reg_valid & ~wb_cp_valid & _T_1180; // @[FPU.scala 708:49]
  assign io_sboard_clr = _T_1171 & (divSqrt_wen | wen[0] & wbInfo_0_pipeid == 2'h3); // @[FPU.scala 709:33]
  assign io_sboard_clra = divSqrt_wen ? divSqrt_waddr : wbInfo_0_rd; // @[FPU.scala 668:18]
  assign io_cp_req_ready = ~ex_reg_valid; // @[FPU.scala 693:22]
  assign io_cp_resp_valid = wbInfo_0_cp & wen[0] | _T_870; // @[FPU.scala 689:33 FPU.scala 691:22]
  assign io_cp_resp_bits_data = wbInfo_0_cp & wen[0] ? wdata : {{1'd0}, _GEN_95}; // @[FPU.scala 689:33 FPU.scala 690:26]
  assign io_cp_resp_bits_exc = 5'h0;
  assign fp_decoder_io_inst = io_inst; // @[FPU.scala 521:22]
  assign sfma_clock = clock;
  assign sfma_reset = reset;
  assign sfma_io_in_valid = req_valid & ex_ctrl_fma & ex_ctrl_single; // @[FPU.scala 585:48]
  assign sfma_io_in_bits_cmd = ex_cp_valid ? io_cp_req_bits_cmd : ex_ctrl_cmd; // @[FPU.scala 576:22 FPU.scala 577:9 FPU.scala 570:7]
  assign sfma_io_in_bits_ren3 = ex_cp_valid ? io_cp_req_bits_ren3 : ex_ctrl_ren3; // @[FPU.scala 576:22 FPU.scala 577:9 FPU.scala 570:7]
  assign sfma_io_in_bits_swap23 = ex_cp_valid ? io_cp_req_bits_swap23 : ex_ctrl_swap23; // @[FPU.scala 576:22 FPU.scala 577:9 FPU.scala 570:7]
  assign sfma_io_in_bits_rm = ex_cp_valid ? io_cp_req_bits_rm : ex_rm; // @[FPU.scala 576:22 FPU.scala 577:9 FPU.scala 571:10]
  assign sfma_io_in_bits_in1 = ex_cp_valid ? io_cp_req_bits_in1 : regfile__T_849_data; // @[FPU.scala 576:22 FPU.scala 577:9 FPU.scala 572:11]
  assign sfma_io_in_bits_in2 = ex_cp_valid ? _GEN_72 : regfile__T_853_data; // @[FPU.scala 576:22 FPU.scala 573:11]
  assign sfma_io_in_bits_in3 = ex_cp_valid ? _GEN_73 : regfile__T_857_data; // @[FPU.scala 576:22 FPU.scala 574:11]
  assign fpiu_clock = clock;
  assign fpiu_io_in_valid = req_valid & (ex_ctrl_toint | ex_ctrl_div | ex_ctrl_sqrt | 5'h5 == _T_865); // @[FPU.scala 589:33]
  assign fpiu_io_in_bits_cmd = ex_cp_valid ? io_cp_req_bits_cmd : ex_ctrl_cmd; // @[FPU.scala 576:22 FPU.scala 577:9 FPU.scala 570:7]
  assign fpiu_io_in_bits_ldst = ex_cp_valid ? io_cp_req_bits_ldst : ex_ctrl_ldst; // @[FPU.scala 576:22 FPU.scala 577:9 FPU.scala 570:7]
  assign fpiu_io_in_bits_single = ex_cp_valid ? io_cp_req_bits_single : ex_ctrl_single; // @[FPU.scala 576:22 FPU.scala 577:9 FPU.scala 570:7]
  assign fpiu_io_in_bits_rm = ex_cp_valid ? io_cp_req_bits_rm : ex_rm; // @[FPU.scala 576:22 FPU.scala 577:9 FPU.scala 571:10]
  assign fpiu_io_in_bits_typ = ex_cp_valid ? io_cp_req_bits_typ : ex_reg_inst[21:20]; // @[FPU.scala 576:22 FPU.scala 577:9 FPU.scala 575:11]
  assign fpiu_io_in_bits_in1 = ex_cp_valid ? io_cp_req_bits_in1 : regfile__T_849_data; // @[FPU.scala 576:22 FPU.scala 577:9 FPU.scala 572:11]
  assign fpiu_io_in_bits_in2 = ex_cp_valid ? _GEN_72 : regfile__T_853_data; // @[FPU.scala 576:22 FPU.scala 573:11]
  assign ifpu_clock = clock;
  assign ifpu_reset = reset;
  assign ifpu_io_in_valid = req_valid & ex_ctrl_fromint; // @[FPU.scala 599:33]
  assign ifpu_io_in_bits_cmd = ex_cp_valid ? io_cp_req_bits_cmd : ex_ctrl_cmd; // @[FPU.scala 576:22 FPU.scala 577:9 FPU.scala 570:7]
  assign ifpu_io_in_bits_single = ex_cp_valid ? io_cp_req_bits_single : ex_ctrl_single; // @[FPU.scala 576:22 FPU.scala 577:9 FPU.scala 570:7]
  assign ifpu_io_in_bits_rm = ex_cp_valid ? io_cp_req_bits_rm : ex_rm; // @[FPU.scala 576:22 FPU.scala 577:9 FPU.scala 571:10]
  assign ifpu_io_in_bits_typ = ex_cp_valid ? io_cp_req_bits_typ : ex_reg_inst[21:20]; // @[FPU.scala 576:22 FPU.scala 577:9 FPU.scala 575:11]
  assign ifpu_io_in_bits_in1 = ex_cp_valid ? io_cp_req_bits_in1 : {{1'd0}, io_fromint_data}; // @[FPU.scala 601:29]
  assign fpmu_clock = clock;
  assign fpmu_reset = reset;
  assign fpmu_io_in_valid = req_valid & ex_ctrl_fastpipe; // @[FPU.scala 604:33]
  assign fpmu_io_in_bits_cmd = ex_cp_valid ? io_cp_req_bits_cmd : ex_ctrl_cmd; // @[FPU.scala 576:22 FPU.scala 577:9 FPU.scala 570:7]
  assign fpmu_io_in_bits_single = ex_cp_valid ? io_cp_req_bits_single : ex_ctrl_single; // @[FPU.scala 576:22 FPU.scala 577:9 FPU.scala 570:7]
  assign fpmu_io_in_bits_rm = ex_cp_valid ? io_cp_req_bits_rm : ex_rm; // @[FPU.scala 576:22 FPU.scala 577:9 FPU.scala 571:10]
  assign fpmu_io_in_bits_in1 = ex_cp_valid ? io_cp_req_bits_in1 : regfile__T_849_data; // @[FPU.scala 576:22 FPU.scala 577:9 FPU.scala 572:11]
  assign fpmu_io_in_bits_in2 = ex_cp_valid ? _GEN_72 : regfile__T_853_data; // @[FPU.scala 576:22 FPU.scala 573:11]
  assign fpmu_io_lt = fpiu_io_out_bits_lt; // @[FPU.scala 606:14]
  assign FPUFMAPipe_clock = clock;
  assign FPUFMAPipe_reset = reset;
  assign FPUFMAPipe_io_in_valid = _T_859 & ~ex_ctrl_single; // @[FPU.scala 625:56]
  assign FPUFMAPipe_io_in_bits_cmd = ex_cp_valid ? io_cp_req_bits_cmd : ex_ctrl_cmd; // @[FPU.scala 576:22 FPU.scala 577:9 FPU.scala 570:7]
  assign FPUFMAPipe_io_in_bits_ren3 = ex_cp_valid ? io_cp_req_bits_ren3 : ex_ctrl_ren3; // @[FPU.scala 576:22 FPU.scala 577:9 FPU.scala 570:7]
  assign FPUFMAPipe_io_in_bits_swap23 = ex_cp_valid ? io_cp_req_bits_swap23 : ex_ctrl_swap23; // @[FPU.scala 576:22 FPU.scala 577:9 FPU.scala 570:7]
  assign FPUFMAPipe_io_in_bits_rm = ex_cp_valid ? io_cp_req_bits_rm : ex_rm; // @[FPU.scala 576:22 FPU.scala 577:9 FPU.scala 571:10]
  assign FPUFMAPipe_io_in_bits_in1 = ex_cp_valid ? io_cp_req_bits_in1 : regfile__T_849_data; // @[FPU.scala 576:22 FPU.scala 577:9 FPU.scala 572:11]
  assign FPUFMAPipe_io_in_bits_in2 = ex_cp_valid ? _GEN_72 : regfile__T_853_data; // @[FPU.scala 576:22 FPU.scala 573:11]
  assign FPUFMAPipe_io_in_bits_in3 = ex_cp_valid ? _GEN_73 : regfile__T_857_data; // @[FPU.scala 576:22 FPU.scala 574:11]
  assign DivSqrtRecF64_clock = clock;
  assign DivSqrtRecF64_reset = reset;
  assign DivSqrtRecF64_io_inValid = _T_1151 & ~divSqrt_in_flight; // @[FPU.scala 725:76]
  assign DivSqrtRecF64_io_sqrtOp = mem_ctrl_sqrt; // @[FPU.scala 726:23]
  assign DivSqrtRecF64_io_a = fpiu_io_as_double_in1; // @[FPU.scala 727:18]
  assign DivSqrtRecF64_io_b = fpiu_io_as_double_in2; // @[FPU.scala 728:18]
  assign DivSqrtRecF64_io_roundingMode = fpiu_io_as_double_rm[1:0]; // @[FPU.scala 729:29]
  assign RecFNToRecFN_io_in = _T_1207; // @[FPU.scala 747:28]
  assign RecFNToRecFN_io_roundingMode = _T_1203; // @[FPU.scala 748:38]
  always @(posedge clock) begin
    if(regfile__T_782_en & regfile__T_782_mask) begin
      regfile[regfile__T_782_addr] <= regfile__T_782_data; // @[FPU.scala 547:20]
    end
    if(regfile__T_1131_en & regfile__T_1131_mask) begin
      regfile[regfile__T_1131_addr] <= regfile__T_1131_data; // @[FPU.scala 547:20]
    end
    if (reset) begin // @[FPU.scala 509:25]
      ex_reg_valid <= 1'h0; // @[FPU.scala 509:25]
    end else begin
      ex_reg_valid <= io_valid; // @[FPU.scala 509:25]
    end
    if (io_valid) begin // @[Reg.scala 35:19]
      ex_reg_inst <= io_inst; // @[Reg.scala 35:23]
    end
    if (reset) begin // @[FPU.scala 513:26]
      mem_reg_valid <= 1'h0; // @[FPU.scala 513:26]
    end else begin
      mem_reg_valid <= ex_reg_valid & ~io_killx | ex_cp_valid; // @[FPU.scala 513:26]
    end
    if (ex_reg_valid) begin // @[Reg.scala 35:19]
      mem_reg_inst <= ex_reg_inst; // @[Reg.scala 35:23]
    end
    if (reset) begin // @[FPU.scala 515:25]
      mem_cp_valid <= 1'h0; // @[FPU.scala 515:25]
    end else begin
      mem_cp_valid <= ex_cp_valid; // @[FPU.scala 515:25]
    end
    if (reset) begin // @[FPU.scala 517:25]
      wb_reg_valid <= 1'h0; // @[FPU.scala 517:25]
    end else begin
      wb_reg_valid <= mem_reg_valid & (~killm | mem_cp_valid); // @[FPU.scala 517:25]
    end
    if (reset) begin // @[FPU.scala 518:24]
      wb_cp_valid <= 1'h0; // @[FPU.scala 518:24]
    end else begin
      wb_cp_valid <= mem_cp_valid; // @[FPU.scala 518:24]
    end
    if (io_valid) begin // @[Reg.scala 35:19]
      _T_282_cmd <= fp_decoder_io_sigs_cmd; // @[Reg.scala 35:23]
    end
    if (io_valid) begin // @[Reg.scala 35:19]
      _T_282_ldst <= fp_decoder_io_sigs_ldst; // @[Reg.scala 35:23]
    end
    if (io_valid) begin // @[Reg.scala 35:19]
      _T_282_ren3 <= fp_decoder_io_sigs_ren3; // @[Reg.scala 35:23]
    end
    if (io_valid) begin // @[Reg.scala 35:19]
      _T_282_swap23 <= fp_decoder_io_sigs_swap23; // @[Reg.scala 35:23]
    end
    if (io_valid) begin // @[Reg.scala 35:19]
      _T_282_single <= fp_decoder_io_sigs_single; // @[Reg.scala 35:23]
    end
    if (io_valid) begin // @[Reg.scala 35:19]
      _T_282_fromint <= fp_decoder_io_sigs_fromint; // @[Reg.scala 35:23]
    end
    if (io_valid) begin // @[Reg.scala 35:19]
      _T_282_toint <= fp_decoder_io_sigs_toint; // @[Reg.scala 35:23]
    end
    if (io_valid) begin // @[Reg.scala 35:19]
      _T_282_fastpipe <= fp_decoder_io_sigs_fastpipe; // @[Reg.scala 35:23]
    end
    if (io_valid) begin // @[Reg.scala 35:19]
      _T_282_fma <= fp_decoder_io_sigs_fma; // @[Reg.scala 35:23]
    end
    if (io_valid) begin // @[Reg.scala 35:19]
      _T_282_div <= fp_decoder_io_sigs_div; // @[Reg.scala 35:23]
    end
    if (io_valid) begin // @[Reg.scala 35:19]
      _T_282_sqrt <= fp_decoder_io_sigs_sqrt; // @[Reg.scala 35:23]
    end
    if (io_valid) begin // @[Reg.scala 35:19]
      _T_282_wflags <= fp_decoder_io_sigs_wflags; // @[Reg.scala 35:23]
    end
    if (req_valid) begin // @[Reg.scala 35:19]
      if (ex_cp_valid) begin // @[FPU.scala 529:20]
        mem_ctrl_single <= io_cp_req_bits_single;
      end else begin
        mem_ctrl_single <= _T_282_single;
      end
    end
    if (req_valid) begin // @[Reg.scala 35:19]
      if (ex_cp_valid) begin // @[FPU.scala 529:20]
        mem_ctrl_fromint <= io_cp_req_bits_fromint;
      end else begin
        mem_ctrl_fromint <= _T_282_fromint;
      end
    end
    if (req_valid) begin // @[Reg.scala 35:19]
      if (ex_cp_valid) begin // @[FPU.scala 529:20]
        mem_ctrl_toint <= io_cp_req_bits_toint;
      end else begin
        mem_ctrl_toint <= _T_282_toint;
      end
    end
    if (req_valid) begin // @[Reg.scala 35:19]
      if (ex_cp_valid) begin // @[FPU.scala 529:20]
        mem_ctrl_fastpipe <= io_cp_req_bits_fastpipe;
      end else begin
        mem_ctrl_fastpipe <= _T_282_fastpipe;
      end
    end
    if (req_valid) begin // @[Reg.scala 35:19]
      if (ex_cp_valid) begin // @[FPU.scala 529:20]
        mem_ctrl_fma <= io_cp_req_bits_fma;
      end else begin
        mem_ctrl_fma <= _T_282_fma;
      end
    end
    if (req_valid) begin // @[Reg.scala 35:19]
      if (ex_cp_valid) begin // @[FPU.scala 529:20]
        mem_ctrl_div <= io_cp_req_bits_div;
      end else begin
        mem_ctrl_div <= _T_282_div;
      end
    end
    if (req_valid) begin // @[Reg.scala 35:19]
      if (ex_cp_valid) begin // @[FPU.scala 529:20]
        mem_ctrl_sqrt <= io_cp_req_bits_sqrt;
      end else begin
        mem_ctrl_sqrt <= _T_282_sqrt;
      end
    end
    if (req_valid) begin // @[Reg.scala 35:19]
      if (ex_cp_valid) begin // @[FPU.scala 529:20]
        mem_ctrl_wflags <= io_cp_req_bits_wflags;
      end else begin
        mem_ctrl_wflags <= _T_282_wflags;
      end
    end
    if (mem_reg_valid) begin // @[Reg.scala 35:19]
      wb_ctrl_toint <= mem_ctrl_toint; // @[Reg.scala 35:23]
    end
    load_wb <= io_dmem_resp_val; // @[FPU.scala 534:20]
    if (io_dmem_resp_val) begin // @[Reg.scala 35:19]
      load_wb_single <= _T_383; // @[Reg.scala 35:23]
    end
    if (io_dmem_resp_val) begin // @[Reg.scala 35:19]
      load_wb_data <= io_dmem_resp_data; // @[Reg.scala 35:23]
    end
    if (io_dmem_resp_val) begin // @[Reg.scala 35:19]
      load_wb_tag <= io_dmem_resp_tag; // @[Reg.scala 35:23]
    end
    if (io_valid) begin // @[FPU.scala 555:19]
      if (fp_decoder_io_sigs_ren2) begin // @[FPU.scala 560:25]
        if (fp_decoder_io_sigs_swap12) begin // @[FPU.scala 561:29]
          ex_ra1 <= io_inst[24:20]; // @[FPU.scala 561:38]
        end else begin
          ex_ra1 <= _GEN_60;
        end
      end else begin
        ex_ra1 <= _GEN_60;
      end
    end
    if (io_valid) begin // @[FPU.scala 555:19]
      if (fp_decoder_io_sigs_ren2) begin // @[FPU.scala 560:25]
        if (_T_787 & ~fp_decoder_io_sigs_swap23) begin // @[FPU.scala 563:49]
          ex_ra2 <= io_inst[24:20]; // @[FPU.scala 563:58]
        end else begin
          ex_ra2 <= _GEN_61;
        end
      end else begin
        ex_ra2 <= _GEN_61;
      end
    end
    if (io_valid) begin // @[FPU.scala 555:19]
      if (fp_decoder_io_sigs_ren3) begin // @[FPU.scala 565:25]
        ex_ra3 <= io_inst[31:27]; // @[FPU.scala 565:34]
      end else if (fp_decoder_io_sigs_ren2) begin // @[FPU.scala 560:25]
        if (fp_decoder_io_sigs_swap23) begin // @[FPU.scala 562:29]
          ex_ra3 <= io_inst[24:20]; // @[FPU.scala 562:38]
        end
      end
    end
    divSqrt_wen <= _T_1209 & ~divSqrt_killed; // @[FPU.scala 739:29 FPU.scala 740:19 FPU.scala 608:24]
    if (DivSqrtRecF64_io_inValid & divSqrt_inReady) begin // @[FPU.scala 731:50]
      divSqrt_waddr <= mem_reg_inst[11:7]; // @[FPU.scala 735:21]
    end
    if (DivSqrtRecF64_io_inValid & divSqrt_inReady) begin // @[FPU.scala 731:50]
      divSqrt_single <= mem_ctrl_single; // @[FPU.scala 734:22]
    end
    if (reset) begin // @[FPU.scala 614:30]
      divSqrt_in_flight <= 1'h0; // @[FPU.scala 614:30]
    end else if (_T_1209) begin // @[FPU.scala 739:29]
      divSqrt_in_flight <= 1'h0; // @[FPU.scala 742:25]
    end else begin
      divSqrt_in_flight <= _GEN_140;
    end
    if (DivSqrtRecF64_io_inValid & divSqrt_inReady) begin // @[FPU.scala 731:50]
      divSqrt_killed <= killm; // @[FPU.scala 733:22]
    end
    if (reset) begin // @[FPU.scala 645:16]
      wen <= 3'h0; // @[FPU.scala 645:16]
    end else if (mem_wen) begin // @[FPU.scala 654:18]
      if (_T_225) begin // @[FPU.scala 655:19]
        wen <= _T_1021; // @[FPU.scala 656:11]
      end else begin
        wen <= {{1'd0}, wen[2:1]}; // @[FPU.scala 653:7]
      end
    end else begin
      wen <= {{1'd0}, wen[2:1]}; // @[FPU.scala 653:7]
    end
    if (mem_wen) begin // @[FPU.scala 654:18]
      if (~write_port_busy & memLatencyMask[0]) begin // @[FPU.scala 659:52]
        wbInfo_0_rd <= mem_reg_inst[11:7]; // @[FPU.scala 663:22]
      end else begin
        wbInfo_0_rd <= _GEN_98;
      end
    end else begin
      wbInfo_0_rd <= _GEN_98;
    end
    if (mem_wen) begin // @[FPU.scala 654:18]
      if (~write_port_busy & memLatencyMask[0]) begin // @[FPU.scala 659:52]
        wbInfo_0_single <= mem_ctrl_single; // @[FPU.scala 661:26]
      end else begin
        wbInfo_0_single <= _GEN_99;
      end
    end else begin
      wbInfo_0_single <= _GEN_99;
    end
    if (mem_wen) begin // @[FPU.scala 654:18]
      if (~write_port_busy & memLatencyMask[0]) begin // @[FPU.scala 659:52]
        wbInfo_0_cp <= mem_cp_valid; // @[FPU.scala 660:22]
      end else begin
        wbInfo_0_cp <= _GEN_100;
      end
    end else begin
      wbInfo_0_cp <= _GEN_100;
    end
    if (mem_wen) begin // @[FPU.scala 654:18]
      if (~write_port_busy & memLatencyMask[0]) begin // @[FPU.scala 659:52]
        wbInfo_0_pipeid <= _T_1044; // @[FPU.scala 662:26]
      end else begin
        wbInfo_0_pipeid <= _GEN_101;
      end
    end else begin
      wbInfo_0_pipeid <= _GEN_101;
    end
    if (mem_wen) begin // @[FPU.scala 654:18]
      if (~write_port_busy & memLatencyMask[1]) begin // @[FPU.scala 659:52]
        wbInfo_1_rd <= mem_reg_inst[11:7]; // @[FPU.scala 663:22]
      end else begin
        wbInfo_1_rd <= _GEN_102;
      end
    end else begin
      wbInfo_1_rd <= _GEN_102;
    end
    if (mem_wen) begin // @[FPU.scala 654:18]
      if (~write_port_busy & memLatencyMask[1]) begin // @[FPU.scala 659:52]
        wbInfo_1_single <= mem_ctrl_single; // @[FPU.scala 661:26]
      end else begin
        wbInfo_1_single <= _GEN_103;
      end
    end else begin
      wbInfo_1_single <= _GEN_103;
    end
    if (mem_wen) begin // @[FPU.scala 654:18]
      if (~write_port_busy & memLatencyMask[1]) begin // @[FPU.scala 659:52]
        wbInfo_1_cp <= mem_cp_valid; // @[FPU.scala 660:22]
      end else begin
        wbInfo_1_cp <= _GEN_104;
      end
    end else begin
      wbInfo_1_cp <= _GEN_104;
    end
    if (mem_wen) begin // @[FPU.scala 654:18]
      if (~write_port_busy & memLatencyMask[1]) begin // @[FPU.scala 659:52]
        wbInfo_1_pipeid <= _T_1044; // @[FPU.scala 662:26]
      end else begin
        wbInfo_1_pipeid <= _GEN_105;
      end
    end else begin
      wbInfo_1_pipeid <= _GEN_105;
    end
    if (mem_wen) begin // @[FPU.scala 654:18]
      if (~write_port_busy & memLatencyMask[2]) begin // @[FPU.scala 659:52]
        wbInfo_2_rd <= mem_reg_inst[11:7]; // @[FPU.scala 663:22]
      end
    end
    if (mem_wen) begin // @[FPU.scala 654:18]
      if (~write_port_busy & memLatencyMask[2]) begin // @[FPU.scala 659:52]
        wbInfo_2_single <= mem_ctrl_single; // @[FPU.scala 661:26]
      end
    end
    if (mem_wen) begin // @[FPU.scala 654:18]
      if (~write_port_busy & memLatencyMask[2]) begin // @[FPU.scala 659:52]
        wbInfo_2_cp <= mem_cp_valid; // @[FPU.scala 660:22]
      end
    end
    if (mem_wen) begin // @[FPU.scala 654:18]
      if (~write_port_busy & memLatencyMask[2]) begin // @[FPU.scala 659:52]
        wbInfo_2_pipeid <= _T_1044; // @[FPU.scala 662:26]
      end
    end
    if (req_valid) begin // @[Reg.scala 35:19]
      write_port_busy <= _T_1013; // @[Reg.scala 35:23]
    end
    if (_T_1209) begin // @[FPU.scala 739:29]
      _T_1207 <= DivSqrtRecF64_io_out; // @[FPU.scala 741:28]
    end
    if (mem_ctrl_toint) begin // @[Reg.scala 35:19]
      wb_toint_exc <= fpiu_io_out_bits_exc; // @[Reg.scala 35:23]
    end
    if (_T_1209) begin // @[FPU.scala 739:29]
      _T_1205 <= DivSqrtRecF64_io_exceptionFlags; // @[FPU.scala 743:28]
    end
    _T_1180 <= _T_899 | mem_ctrl_div | mem_ctrl_sqrt; // @[FPU.scala 708:112]
    if (DivSqrtRecF64_io_inValid & divSqrt_inReady) begin // @[FPU.scala 731:50]
      _T_1203 <= DivSqrtRecF64_io_roundingMode; // @[FPU.scala 736:18]
    end
  end
// Register and memory initialization
`ifdef RANDOMIZE_GARBAGE_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_INVALID_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_REG_INIT
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_MEM_INIT
`define RANDOMIZE
`endif
`ifndef RANDOM
`define RANDOM $random
`endif
`ifdef RANDOMIZE_MEM_INIT
  integer initvar;
`endif
`ifndef SYNTHESIS
`ifdef FIRRTL_BEFORE_INITIAL
`FIRRTL_BEFORE_INITIAL
`endif
initial begin
  `ifdef RANDOMIZE
    `ifdef INIT_RANDOM
      `INIT_RANDOM
    `endif
    `ifndef VERILATOR
      `ifdef RANDOMIZE_DELAY
        #`RANDOMIZE_DELAY begin end
      `else
        #0.002 begin end
      `endif
    `endif
`ifdef RANDOMIZE_MEM_INIT
  _RAND_0 = {3{`RANDOM}};
  for (initvar = 0; initvar < 32; initvar = initvar+1)
    regfile[initvar] = _RAND_0[64:0];
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  _RAND_1 = {1{`RANDOM}};
  ex_reg_valid = _RAND_1[0:0];
  _RAND_2 = {1{`RANDOM}};
  ex_reg_inst = _RAND_2[31:0];
  _RAND_3 = {1{`RANDOM}};
  mem_reg_valid = _RAND_3[0:0];
  _RAND_4 = {1{`RANDOM}};
  mem_reg_inst = _RAND_4[31:0];
  _RAND_5 = {1{`RANDOM}};
  mem_cp_valid = _RAND_5[0:0];
  _RAND_6 = {1{`RANDOM}};
  wb_reg_valid = _RAND_6[0:0];
  _RAND_7 = {1{`RANDOM}};
  wb_cp_valid = _RAND_7[0:0];
  _RAND_8 = {1{`RANDOM}};
  _T_282_cmd = _RAND_8[4:0];
  _RAND_9 = {1{`RANDOM}};
  _T_282_ldst = _RAND_9[0:0];
  _RAND_10 = {1{`RANDOM}};
  _T_282_ren3 = _RAND_10[0:0];
  _RAND_11 = {1{`RANDOM}};
  _T_282_swap23 = _RAND_11[0:0];
  _RAND_12 = {1{`RANDOM}};
  _T_282_single = _RAND_12[0:0];
  _RAND_13 = {1{`RANDOM}};
  _T_282_fromint = _RAND_13[0:0];
  _RAND_14 = {1{`RANDOM}};
  _T_282_toint = _RAND_14[0:0];
  _RAND_15 = {1{`RANDOM}};
  _T_282_fastpipe = _RAND_15[0:0];
  _RAND_16 = {1{`RANDOM}};
  _T_282_fma = _RAND_16[0:0];
  _RAND_17 = {1{`RANDOM}};
  _T_282_div = _RAND_17[0:0];
  _RAND_18 = {1{`RANDOM}};
  _T_282_sqrt = _RAND_18[0:0];
  _RAND_19 = {1{`RANDOM}};
  _T_282_wflags = _RAND_19[0:0];
  _RAND_20 = {1{`RANDOM}};
  mem_ctrl_single = _RAND_20[0:0];
  _RAND_21 = {1{`RANDOM}};
  mem_ctrl_fromint = _RAND_21[0:0];
  _RAND_22 = {1{`RANDOM}};
  mem_ctrl_toint = _RAND_22[0:0];
  _RAND_23 = {1{`RANDOM}};
  mem_ctrl_fastpipe = _RAND_23[0:0];
  _RAND_24 = {1{`RANDOM}};
  mem_ctrl_fma = _RAND_24[0:0];
  _RAND_25 = {1{`RANDOM}};
  mem_ctrl_div = _RAND_25[0:0];
  _RAND_26 = {1{`RANDOM}};
  mem_ctrl_sqrt = _RAND_26[0:0];
  _RAND_27 = {1{`RANDOM}};
  mem_ctrl_wflags = _RAND_27[0:0];
  _RAND_28 = {1{`RANDOM}};
  wb_ctrl_toint = _RAND_28[0:0];
  _RAND_29 = {1{`RANDOM}};
  load_wb = _RAND_29[0:0];
  _RAND_30 = {1{`RANDOM}};
  load_wb_single = _RAND_30[0:0];
  _RAND_31 = {2{`RANDOM}};
  load_wb_data = _RAND_31[63:0];
  _RAND_32 = {1{`RANDOM}};
  load_wb_tag = _RAND_32[4:0];
  _RAND_33 = {1{`RANDOM}};
  ex_ra1 = _RAND_33[4:0];
  _RAND_34 = {1{`RANDOM}};
  ex_ra2 = _RAND_34[4:0];
  _RAND_35 = {1{`RANDOM}};
  ex_ra3 = _RAND_35[4:0];
  _RAND_36 = {1{`RANDOM}};
  divSqrt_wen = _RAND_36[0:0];
  _RAND_37 = {1{`RANDOM}};
  divSqrt_waddr = _RAND_37[4:0];
  _RAND_38 = {1{`RANDOM}};
  divSqrt_single = _RAND_38[0:0];
  _RAND_39 = {1{`RANDOM}};
  divSqrt_in_flight = _RAND_39[0:0];
  _RAND_40 = {1{`RANDOM}};
  divSqrt_killed = _RAND_40[0:0];
  _RAND_41 = {1{`RANDOM}};
  wen = _RAND_41[2:0];
  _RAND_42 = {1{`RANDOM}};
  wbInfo_0_rd = _RAND_42[4:0];
  _RAND_43 = {1{`RANDOM}};
  wbInfo_0_single = _RAND_43[0:0];
  _RAND_44 = {1{`RANDOM}};
  wbInfo_0_cp = _RAND_44[0:0];
  _RAND_45 = {1{`RANDOM}};
  wbInfo_0_pipeid = _RAND_45[1:0];
  _RAND_46 = {1{`RANDOM}};
  wbInfo_1_rd = _RAND_46[4:0];
  _RAND_47 = {1{`RANDOM}};
  wbInfo_1_single = _RAND_47[0:0];
  _RAND_48 = {1{`RANDOM}};
  wbInfo_1_cp = _RAND_48[0:0];
  _RAND_49 = {1{`RANDOM}};
  wbInfo_1_pipeid = _RAND_49[1:0];
  _RAND_50 = {1{`RANDOM}};
  wbInfo_2_rd = _RAND_50[4:0];
  _RAND_51 = {1{`RANDOM}};
  wbInfo_2_single = _RAND_51[0:0];
  _RAND_52 = {1{`RANDOM}};
  wbInfo_2_cp = _RAND_52[0:0];
  _RAND_53 = {1{`RANDOM}};
  wbInfo_2_pipeid = _RAND_53[1:0];
  _RAND_54 = {1{`RANDOM}};
  write_port_busy = _RAND_54[0:0];
  _RAND_55 = {3{`RANDOM}};
  _T_1207 = _RAND_55[64:0];
  _RAND_56 = {1{`RANDOM}};
  wb_toint_exc = _RAND_56[4:0];
  _RAND_57 = {1{`RANDOM}};
  _T_1205 = _RAND_57[4:0];
  _RAND_58 = {1{`RANDOM}};
  _T_1180 = _RAND_58[0:0];
  _RAND_59 = {1{`RANDOM}};
  _T_1203 = _RAND_59[1:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module FPUDecoder(
  input  [31:0] io_inst,
  output [4:0]  io_sigs_cmd,
  output        io_sigs_ldst,
  output        io_sigs_wen,
  output        io_sigs_ren1,
  output        io_sigs_ren2,
  output        io_sigs_ren3,
  output        io_sigs_swap12,
  output        io_sigs_swap23,
  output        io_sigs_single,
  output        io_sigs_fromint,
  output        io_sigs_toint,
  output        io_sigs_fastpipe,
  output        io_sigs_fma,
  output        io_sigs_div,
  output        io_sigs_sqrt,
  output        io_sigs_wflags
);
  wire [31:0] _T_22 = io_inst & 32'h4; // @[Decode.scala 13:65]
  wire  _T_24 = _T_22 == 32'h4; // @[Decode.scala 13:121]
  wire [31:0] _T_26 = io_inst & 32'h8000010; // @[Decode.scala 13:65]
  wire  _T_28 = _T_26 == 32'h8000010; // @[Decode.scala 13:121]
  wire  _T_31 = _T_24 | _T_28; // @[Decode.scala 14:30]
  wire [31:0] _T_33 = io_inst & 32'h8; // @[Decode.scala 13:65]
  wire  _T_35 = _T_33 == 32'h8; // @[Decode.scala 13:121]
  wire [31:0] _T_37 = io_inst & 32'h10000010; // @[Decode.scala 13:65]
  wire  _T_39 = _T_37 == 32'h10000010; // @[Decode.scala 13:121]
  wire  _T_42 = _T_35 | _T_39; // @[Decode.scala 14:30]
  wire [31:0] _T_44 = io_inst & 32'h40; // @[Decode.scala 13:65]
  wire  decoder_1 = _T_44 == 32'h0; // @[Decode.scala 13:121]
  wire [31:0] _T_48 = io_inst & 32'h20000000; // @[Decode.scala 13:65]
  wire  _T_50 = _T_48 == 32'h20000000; // @[Decode.scala 13:121]
  wire  _T_53 = decoder_1 | _T_50; // @[Decode.scala 14:30]
  wire [31:0] _T_55 = io_inst & 32'h40000000; // @[Decode.scala 13:65]
  wire  _T_57 = _T_55 == 32'h40000000; // @[Decode.scala 13:121]
  wire  _T_60 = decoder_1 | _T_57; // @[Decode.scala 14:30]
  wire [31:0] _T_62 = io_inst & 32'h10; // @[Decode.scala 13:65]
  wire  _T_64 = _T_62 == 32'h0; // @[Decode.scala 13:121]
  wire [1:0] _T_67 = {_T_42,_T_31}; // @[Cat.scala 30:58]
  wire [2:0] _T_69 = {_T_64,_T_60,_T_53}; // @[Cat.scala 30:58]
  wire [31:0] _T_72 = io_inst & 32'h80000020; // @[Decode.scala 13:65]
  wire  _T_74 = _T_72 == 32'h0; // @[Decode.scala 13:121]
  wire [31:0] _T_76 = io_inst & 32'h30; // @[Decode.scala 13:65]
  wire  _T_78 = _T_76 == 32'h0; // @[Decode.scala 13:121]
  wire [31:0] _T_80 = io_inst & 32'h10000020; // @[Decode.scala 13:65]
  wire  _T_82 = _T_80 == 32'h10000000; // @[Decode.scala 13:121]
  wire [31:0] _T_87 = io_inst & 32'h80000004; // @[Decode.scala 13:65]
  wire  _T_89 = _T_87 == 32'h0; // @[Decode.scala 13:121]
  wire [31:0] _T_91 = io_inst & 32'h10000004; // @[Decode.scala 13:65]
  wire  _T_93 = _T_91 == 32'h0; // @[Decode.scala 13:121]
  wire [31:0] _T_95 = io_inst & 32'h50; // @[Decode.scala 13:65]
  wire  decoder_5 = _T_95 == 32'h40; // @[Decode.scala 13:121]
  wire [31:0] _T_102 = io_inst & 32'h40000004; // @[Decode.scala 13:65]
  wire  _T_104 = _T_102 == 32'h0; // @[Decode.scala 13:121]
  wire [31:0] _T_106 = io_inst & 32'h20; // @[Decode.scala 13:65]
  wire  _T_108 = _T_106 == 32'h20; // @[Decode.scala 13:121]
  wire [31:0] _T_114 = io_inst & 32'h50000010; // @[Decode.scala 13:65]
  wire  _T_116 = _T_114 == 32'h50000010; // @[Decode.scala 13:121]
  wire [31:0] _T_120 = io_inst & 32'h30000010; // @[Decode.scala 13:65]
  wire [31:0] _T_125 = io_inst & 32'h1040; // @[Decode.scala 13:65]
  wire  _T_127 = _T_125 == 32'h0; // @[Decode.scala 13:121]
  wire [31:0] _T_129 = io_inst & 32'h2000040; // @[Decode.scala 13:65]
  wire  _T_131 = _T_129 == 32'h40; // @[Decode.scala 13:121]
  wire [31:0] _T_135 = io_inst & 32'h90000010; // @[Decode.scala 13:65]
  wire  _T_142 = _T_135 == 32'h80000010; // @[Decode.scala 13:121]
  wire [31:0] _T_146 = io_inst & 32'ha0000010; // @[Decode.scala 13:65]
  wire  _T_148 = _T_146 == 32'h20000010; // @[Decode.scala 13:121]
  wire [31:0] _T_150 = io_inst & 32'hd0000010; // @[Decode.scala 13:65]
  wire  _T_152 = _T_150 == 32'h40000010; // @[Decode.scala 13:121]
  wire [31:0] _T_156 = io_inst & 32'h70000004; // @[Decode.scala 13:65]
  wire  _T_158 = _T_156 == 32'h0; // @[Decode.scala 13:121]
  wire [31:0] _T_160 = io_inst & 32'h68000004; // @[Decode.scala 13:65]
  wire  _T_162 = _T_160 == 32'h0; // @[Decode.scala 13:121]
  wire [31:0] _T_167 = io_inst & 32'h58000010; // @[Decode.scala 13:65]
  wire [31:0] _T_177 = io_inst & 32'h20000004; // @[Decode.scala 13:65]
  wire  _T_179 = _T_177 == 32'h0; // @[Decode.scala 13:121]
  wire [31:0] _T_181 = io_inst & 32'h8002000; // @[Decode.scala 13:65]
  wire  _T_183 = _T_181 == 32'h8000000; // @[Decode.scala 13:121]
  wire [31:0] _T_185 = io_inst & 32'hc0000004; // @[Decode.scala 13:65]
  wire  _T_187 = _T_185 == 32'h80000000; // @[Decode.scala 13:121]
  assign io_sigs_cmd = {_T_69,_T_67}; // @[Cat.scala 30:58]
  assign io_sigs_ldst = _T_44 == 32'h0; // @[Decode.scala 13:121]
  assign io_sigs_wen = _T_74 | _T_78 | _T_82; // @[Decode.scala 14:30]
  assign io_sigs_ren1 = _T_89 | _T_93 | decoder_5; // @[Decode.scala 14:30]
  assign io_sigs_ren2 = _T_104 | _T_108 | decoder_5; // @[Decode.scala 14:30]
  assign io_sigs_ren3 = _T_95 == 32'h40; // @[Decode.scala 13:121]
  assign io_sigs_swap12 = decoder_1 | _T_116; // @[Decode.scala 14:30]
  assign io_sigs_swap23 = _T_120 == 32'h10; // @[Decode.scala 13:121]
  assign io_sigs_single = _T_127 | _T_131; // @[Decode.scala 14:30]
  assign io_sigs_fromint = _T_135 == 32'h90000010; // @[Decode.scala 13:121]
  assign io_sigs_toint = _T_108 | _T_142; // @[Decode.scala 14:30]
  assign io_sigs_fastpipe = _T_148 | _T_152; // @[Decode.scala 14:30]
  assign io_sigs_fma = _T_158 | _T_162 | decoder_5; // @[Decode.scala 14:30]
  assign io_sigs_div = _T_167 == 32'h18000010; // @[Decode.scala 13:121]
  assign io_sigs_sqrt = _T_150 == 32'h50000010; // @[Decode.scala 13:121]
  assign io_sigs_wflags = _T_179 | decoder_5 | _T_183 | _T_187; // @[Decode.scala 14:30]
endmodule
module FPUFMAPipe(
  input         clock,
  input         reset,
  input         io_in_valid,
  input  [4:0]  io_in_bits_cmd,
  input         io_in_bits_ren3,
  input         io_in_bits_swap23,
  input  [2:0]  io_in_bits_rm,
  input  [64:0] io_in_bits_in1,
  input  [64:0] io_in_bits_in2,
  input  [64:0] io_in_bits_in3,
  output [64:0] io_out_bits_data,
  output [4:0]  io_out_bits_exc
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [95:0] _RAND_3;
  reg [95:0] _RAND_4;
  reg [95:0] _RAND_5;
  reg [31:0] _RAND_6;
  reg [95:0] _RAND_7;
  reg [31:0] _RAND_8;
  reg [95:0] _RAND_9;
  reg [31:0] _RAND_10;
`endif // RANDOMIZE_REG_INIT
  wire [1:0] fma_io_op; // @[FPU.scala 493:19]
  wire [32:0] fma_io_a; // @[FPU.scala 493:19]
  wire [32:0] fma_io_b; // @[FPU.scala 493:19]
  wire [32:0] fma_io_c; // @[FPU.scala 493:19]
  wire [1:0] fma_io_roundingMode; // @[FPU.scala 493:19]
  wire [32:0] fma_io_out; // @[FPU.scala 493:19]
  wire [4:0] fma_io_exceptionFlags; // @[FPU.scala 493:19]
  wire  _T_133 = io_in_bits_in1[32] ^ io_in_bits_in2[32]; // @[FPU.scala 480:37]
  wire [32:0] zero = {_T_133, 32'h0}; // @[FPU.scala 480:62]
  reg  valid; // @[FPU.scala 482:18]
  reg [4:0] in_cmd; // @[FPU.scala 483:15]
  reg [2:0] in_rm; // @[FPU.scala 483:15]
  reg [64:0] in_in1; // @[FPU.scala 483:15]
  reg [64:0] in_in2; // @[FPU.scala 483:15]
  reg [64:0] in_in3; // @[FPU.scala 483:15]
  wire  _T_178 = io_in_bits_ren3 | io_in_bits_swap23; // @[FPU.scala 488:48]
  wire  _T_179 = io_in_bits_cmd[1] & (io_in_bits_ren3 | io_in_bits_swap23); // @[FPU.scala 488:37]
  wire [1:0] _T_181 = {_T_179,io_in_bits_cmd[0]}; // @[Cat.scala 30:58]
  reg  _T_192; // @[Valid.scala 47:18]
  reg [64:0] _T_196_data; // @[Reg.scala 34:16]
  reg [4:0] _T_196_exc; // @[Reg.scala 34:16]
  wire [64:0] res_data = {{32'd0}, fma_io_out}; // @[FPU.scala 500:17 FPU.scala 501:12]
  wire [4:0] res_exc = fma_io_exceptionFlags; // @[FPU.scala 500:17 FPU.scala 502:11]
  reg [64:0] _T_205_data; // @[Reg.scala 34:16]
  reg [4:0] _T_205_exc; // @[Reg.scala 34:16]
  MulAddRecFN fma ( // @[FPU.scala 493:19]
    .io_op(fma_io_op),
    .io_a(fma_io_a),
    .io_b(fma_io_b),
    .io_c(fma_io_c),
    .io_roundingMode(fma_io_roundingMode),
    .io_out(fma_io_out),
    .io_exceptionFlags(fma_io_exceptionFlags)
  );
  assign io_out_bits_data = _T_205_data; // @[Valid.scala 42:21 Valid.scala 44:16]
  assign io_out_bits_exc = _T_205_exc; // @[Valid.scala 42:21 Valid.scala 44:16]
  assign fma_io_op = in_cmd[1:0]; // @[FPU.scala 494:13]
  assign fma_io_a = in_in1[32:0]; // @[FPU.scala 496:12]
  assign fma_io_b = in_in2[32:0]; // @[FPU.scala 497:12]
  assign fma_io_c = in_in3[32:0]; // @[FPU.scala 498:12]
  assign fma_io_roundingMode = in_rm[1:0]; // @[FPU.scala 495:23]
  always @(posedge clock) begin
    valid <= io_in_valid; // @[FPU.scala 482:18]
    if (io_in_valid) begin // @[FPU.scala 484:22]
      in_cmd <= {{3'd0}, _T_181}; // @[FPU.scala 488:12]
    end
    if (io_in_valid) begin // @[FPU.scala 484:22]
      in_rm <= io_in_bits_rm; // @[FPU.scala 485:8]
    end
    if (io_in_valid) begin // @[FPU.scala 484:22]
      in_in1 <= io_in_bits_in1; // @[FPU.scala 485:8]
    end
    if (io_in_valid) begin // @[FPU.scala 484:22]
      if (io_in_bits_swap23) begin // @[FPU.scala 489:23]
        in_in2 <= 65'h80000000; // @[FPU.scala 489:32]
      end else begin
        in_in2 <= io_in_bits_in2; // @[FPU.scala 485:8]
      end
    end
    if (io_in_valid) begin // @[FPU.scala 484:22]
      if (~_T_178) begin // @[Conditional.scala 19:15]
        in_in3 <= {{32'd0}, zero}; // @[FPU.scala 490:45]
      end else begin
        in_in3 <= io_in_bits_in3; // @[FPU.scala 485:8]
      end
    end
    if (reset) begin // @[Valid.scala 47:18]
      _T_192 <= 1'h0; // @[Valid.scala 47:18]
    end else begin
      _T_192 <= valid; // @[Valid.scala 47:18]
    end
    if (valid) begin // @[Reg.scala 35:19]
      _T_196_data <= res_data; // @[Reg.scala 35:23]
    end
    if (valid) begin // @[Reg.scala 35:19]
      _T_196_exc <= res_exc; // @[Reg.scala 35:23]
    end
    if (_T_192) begin // @[Reg.scala 35:19]
      _T_205_data <= _T_196_data; // @[Reg.scala 35:23]
    end
    if (_T_192) begin // @[Reg.scala 35:19]
      _T_205_exc <= _T_196_exc; // @[Reg.scala 35:23]
    end
  end
// Register and memory initialization
`ifdef RANDOMIZE_GARBAGE_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_INVALID_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_REG_INIT
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_MEM_INIT
`define RANDOMIZE
`endif
`ifndef RANDOM
`define RANDOM $random
`endif
`ifdef RANDOMIZE_MEM_INIT
  integer initvar;
`endif
`ifndef SYNTHESIS
`ifdef FIRRTL_BEFORE_INITIAL
`FIRRTL_BEFORE_INITIAL
`endif
initial begin
  `ifdef RANDOMIZE
    `ifdef INIT_RANDOM
      `INIT_RANDOM
    `endif
    `ifndef VERILATOR
      `ifdef RANDOMIZE_DELAY
        #`RANDOMIZE_DELAY begin end
      `else
        #0.002 begin end
      `endif
    `endif
`ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  valid = _RAND_0[0:0];
  _RAND_1 = {1{`RANDOM}};
  in_cmd = _RAND_1[4:0];
  _RAND_2 = {1{`RANDOM}};
  in_rm = _RAND_2[2:0];
  _RAND_3 = {3{`RANDOM}};
  in_in1 = _RAND_3[64:0];
  _RAND_4 = {3{`RANDOM}};
  in_in2 = _RAND_4[64:0];
  _RAND_5 = {3{`RANDOM}};
  in_in3 = _RAND_5[64:0];
  _RAND_6 = {1{`RANDOM}};
  _T_192 = _RAND_6[0:0];
  _RAND_7 = {3{`RANDOM}};
  _T_196_data = _RAND_7[64:0];
  _RAND_8 = {1{`RANDOM}};
  _T_196_exc = _RAND_8[4:0];
  _RAND_9 = {3{`RANDOM}};
  _T_205_data = _RAND_9[64:0];
  _RAND_10 = {1{`RANDOM}};
  _T_205_exc = _RAND_10[4:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module FPToInt(
  input         clock,
  input         io_in_valid,
  input  [4:0]  io_in_bits_cmd,
  input         io_in_bits_ldst,
  input         io_in_bits_single,
  input  [2:0]  io_in_bits_rm,
  input  [1:0]  io_in_bits_typ,
  input  [64:0] io_in_bits_in1,
  input  [64:0] io_in_bits_in2,
  output [2:0]  io_as_double_rm,
  output [64:0] io_as_double_in1,
  output [64:0] io_as_double_in2,
  output        io_out_valid,
  output        io_out_bits_lt,
  output [63:0] io_out_bits_store,
  output [63:0] io_out_bits_toint,
  output [4:0]  io_out_bits_exc
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_3;
  reg [95:0] _RAND_4;
  reg [95:0] _RAND_5;
  reg [31:0] _RAND_6;
`endif // RANDOMIZE_REG_INIT
  wire [64:0] dcmp_io_a; // @[FPU.scala 319:20]
  wire [64:0] dcmp_io_b; // @[FPU.scala 319:20]
  wire  dcmp_io_signaling; // @[FPU.scala 319:20]
  wire  dcmp_io_lt; // @[FPU.scala 319:20]
  wire  dcmp_io_eq; // @[FPU.scala 319:20]
  wire [4:0] dcmp_io_exceptionFlags; // @[FPU.scala 319:20]
  wire [64:0] RecFNToIN_io_in; // @[FPU.scala 336:24]
  wire [1:0] RecFNToIN_io_roundingMode; // @[FPU.scala 336:24]
  wire  RecFNToIN_io_signedOut; // @[FPU.scala 336:24]
  wire [31:0] RecFNToIN_io_out; // @[FPU.scala 336:24]
  wire [2:0] RecFNToIN_io_intExceptionFlags; // @[FPU.scala 336:24]
  wire [64:0] RecFNToIN_1_io_in; // @[FPU.scala 336:24]
  wire [1:0] RecFNToIN_1_io_roundingMode; // @[FPU.scala 336:24]
  wire  RecFNToIN_1_io_signedOut; // @[FPU.scala 336:24]
  wire [63:0] RecFNToIN_1_io_out; // @[FPU.scala 336:24]
  wire [2:0] RecFNToIN_1_io_intExceptionFlags; // @[FPU.scala 336:24]
  reg [4:0] in_cmd; // @[FPU.scala 286:15]
  reg  in_single; // @[FPU.scala 286:15]
  reg [2:0] in_rm; // @[FPU.scala 286:15]
  reg [1:0] in_typ; // @[FPU.scala 286:15]
  reg [64:0] in_in1; // @[FPU.scala 286:15]
  reg [64:0] in_in2; // @[FPU.scala 286:15]
  reg  valid; // @[FPU.scala 287:18]
  wire [4:0] _T_228 = io_in_bits_cmd & 5'hc; // @[FPU.scala 293:82]
  wire [75:0] _T_236 = {io_in_bits_in1[22:0], 53'h0}; // @[FPU.scala 240:28]
  wire [11:0] _GEN_31 = {{3'd0}, io_in_bits_in1[31:23]}; // @[FPU.scala 243:31]
  wire [11:0] _T_241 = _GEN_31 + 12'h800; // @[FPU.scala 243:31]
  wire [11:0] _T_245 = _T_241 - 12'h100; // @[FPU.scala 243:53]
  wire [11:0] _T_252 = {io_in_bits_in1[31:29],_T_245[8:0]}; // @[Cat.scala 30:58]
  wire [11:0] _T_254 = io_in_bits_in1[31:29] == 3'h0 | io_in_bits_in1[31:29] >= 3'h6 ? _T_252 : _T_245; // @[FPU.scala 244:10]
  wire [64:0] _T_256 = {io_in_bits_in1[32],_T_254,_T_236[75:24]}; // @[Cat.scala 30:58]
  wire [75:0] _T_260 = {io_in_bits_in2[22:0], 53'h0}; // @[FPU.scala 240:28]
  wire [11:0] _GEN_32 = {{3'd0}, io_in_bits_in2[31:23]}; // @[FPU.scala 243:31]
  wire [11:0] _T_265 = _GEN_32 + 12'h800; // @[FPU.scala 243:31]
  wire [11:0] _T_269 = _T_265 - 12'h100; // @[FPU.scala 243:53]
  wire [11:0] _T_276 = {io_in_bits_in2[31:29],_T_269[8:0]}; // @[Cat.scala 30:58]
  wire [11:0] _T_278 = io_in_bits_in2[31:29] == 3'h0 | io_in_bits_in2[31:29] >= 3'h6 ? _T_276 : _T_269; // @[FPU.scala 244:10]
  wire [64:0] _T_280 = {io_in_bits_in2[32],_T_278,_T_260[75:24]}; // @[Cat.scala 30:58]
  wire  _T_286 = in_in1[29:23] < 7'h2; // @[fNFromRecFN.scala 49:57]
  wire  _T_289 = in_in1[31:29] == 3'h1; // @[fNFromRecFN.scala 51:44]
  wire  _T_292 = in_in1[31:30] == 2'h1; // @[fNFromRecFN.scala 52:49]
  wire  _T_293 = in_in1[31:30] == 2'h1 & _T_286; // @[fNFromRecFN.scala 52:62]
  wire  _T_294 = in_in1[31:29] == 3'h1 | _T_293; // @[fNFromRecFN.scala 51:57]
  wire  _T_299 = ~_T_286; // @[fNFromRecFN.scala 56:18]
  wire  _T_300 = _T_292 & _T_299; // @[fNFromRecFN.scala 55:58]
  wire  _T_303 = in_in1[31:30] == 2'h2; // @[fNFromRecFN.scala 57:48]
  wire  _T_304 = _T_300 | _T_303; // @[fNFromRecFN.scala 56:39]
  wire  _T_307 = in_in1[31:30] == 2'h3; // @[fNFromRecFN.scala 58:55]
  wire  _T_309 = _T_307 & in_in1[29]; // @[fNFromRecFN.scala 59:31]
  wire [4:0] _T_314 = 5'h2 - in_in1[27:23]; // @[fNFromRecFN.scala 61:39]
  wire [23:0] _T_316 = {1'h1,in_in1[22:0]}; // @[Cat.scala 30:58]
  wire [23:0] _T_317 = _T_316 >> _T_314; // @[fNFromRecFN.scala 63:35]
  wire [7:0] _T_323 = in_in1[30:23] - 8'h81; // @[fNFromRecFN.scala 65:36]
  wire [7:0] _T_327 = _T_307 ? 8'hff : 8'h0; // @[Bitwise.scala 71:12]
  wire [7:0] _T_328 = _T_304 ? _T_323 : _T_327; // @[fNFromRecFN.scala 68:16]
  wire [22:0] _T_331 = _T_294 ? _T_317[22:0] : 23'h0; // @[fNFromRecFN.scala 72:20]
  wire [22:0] _T_332 = _T_304 | _T_309 ? in_in1[22:0] : _T_331; // @[fNFromRecFN.scala 70:16]
  wire [31:0] _T_334 = {in_in1[32],_T_328,_T_332}; // @[Cat.scala 30:58]
  wire [31:0] _T_339 = _T_334[31] ? 32'hffffffff : 32'h0; // @[Bitwise.scala 71:12]
  wire [63:0] unrec_s = {_T_339,in_in1[32],_T_328,_T_332}; // @[Cat.scala 30:58]
  wire  _T_345 = in_in1[61:52] < 10'h2; // @[fNFromRecFN.scala 49:57]
  wire  _T_348 = in_in1[63:61] == 3'h1; // @[fNFromRecFN.scala 51:44]
  wire  _T_351 = in_in1[63:62] == 2'h1; // @[fNFromRecFN.scala 52:49]
  wire  _T_352 = in_in1[63:62] == 2'h1 & _T_345; // @[fNFromRecFN.scala 52:62]
  wire  _T_353 = in_in1[63:61] == 3'h1 | _T_352; // @[fNFromRecFN.scala 51:57]
  wire  _T_358 = ~_T_345; // @[fNFromRecFN.scala 56:18]
  wire  _T_359 = _T_351 & _T_358; // @[fNFromRecFN.scala 55:58]
  wire  _T_362 = in_in1[63:62] == 2'h2; // @[fNFromRecFN.scala 57:48]
  wire  _T_363 = _T_359 | _T_362; // @[fNFromRecFN.scala 56:39]
  wire  _T_366 = in_in1[63:62] == 2'h3; // @[fNFromRecFN.scala 58:55]
  wire  _T_368 = _T_366 & in_in1[61]; // @[fNFromRecFN.scala 59:31]
  wire [5:0] _T_373 = 6'h2 - in_in1[57:52]; // @[fNFromRecFN.scala 61:39]
  wire [52:0] _T_375 = {1'h1,in_in1[51:0]}; // @[Cat.scala 30:58]
  wire [52:0] _T_376 = _T_375 >> _T_373; // @[fNFromRecFN.scala 63:35]
  wire [10:0] _T_382 = in_in1[62:52] - 11'h401; // @[fNFromRecFN.scala 65:36]
  wire [10:0] _T_386 = _T_366 ? 11'h7ff : 11'h0; // @[Bitwise.scala 71:12]
  wire [10:0] _T_387 = _T_363 ? _T_382 : _T_386; // @[fNFromRecFN.scala 68:16]
  wire [51:0] _T_390 = _T_353 ? _T_376[51:0] : 52'h0; // @[fNFromRecFN.scala 72:20]
  wire [51:0] _T_391 = _T_363 | _T_368 ? in_in1[51:0] : _T_390; // @[fNFromRecFN.scala 70:16]
  wire [63:0] _T_393 = {in_in1[64],_T_387,_T_391}; // @[Cat.scala 30:58]
  wire [63:0] unrec_int = in_single ? unrec_s : _T_393; // @[FPU.scala 304:10]
  wire  _T_400 = in_in1[31:30] == 2'h3; // @[FPU.scala 207:30]
  wire  _T_407 = in_in1[31:30] == 2'h1; // @[FPU.scala 210:50]
  wire  _T_409 = _T_289 | in_in1[31:30] == 2'h1 & _T_286; // @[FPU.scala 210:40]
  wire  _T_417 = _T_407 & _T_299 | in_in1[31:30] == 2'h2; // @[FPU.scala 211:61]
  wire  _T_419 = in_in1[31:29] == 3'h0; // @[FPU.scala 212:23]
  wire  _T_423 = _T_400 & ~in_in1[29]; // @[FPU.scala 213:27]
  wire [2:0] _T_424 = ~in_in1[31:29]; // @[FPU.scala 214:22]
  wire  _T_426 = _T_424 == 3'h0; // @[FPU.scala 214:22]
  wire  _T_430 = _T_426 & ~in_in1[22]; // @[FPU.scala 215:24]
  wire  _T_432 = _T_426 & in_in1[22]; // @[FPU.scala 216:24]
  wire  _T_434 = ~in_in1[32]; // @[FPU.scala 218:34]
  wire  _T_435 = _T_423 & ~in_in1[32]; // @[FPU.scala 218:31]
  wire  _T_438 = _T_417 & ~in_in1[32]; // @[FPU.scala 218:50]
  wire  _T_441 = _T_409 & _T_434; // @[FPU.scala 219:21]
  wire  _T_444 = _T_419 & _T_434; // @[FPU.scala 219:38]
  wire  _T_445 = _T_419 & in_in1[32]; // @[FPU.scala 219:55]
  wire  _T_446 = _T_409 & in_in1[32]; // @[FPU.scala 220:21]
  wire  _T_447 = _T_417 & in_in1[32]; // @[FPU.scala 220:39]
  wire  _T_448 = _T_423 & in_in1[32]; // @[FPU.scala 220:54]
  wire [9:0] classify_s = {_T_432,_T_430,_T_435,_T_438,_T_441,_T_444,_T_445,_T_446,_T_447,_T_448}; // @[Cat.scala 30:58]
  wire  _T_463 = in_in1[63:62] == 2'h3; // @[FPU.scala 207:30]
  wire  _T_470 = in_in1[63:62] == 2'h1; // @[FPU.scala 210:50]
  wire  _T_472 = _T_348 | in_in1[63:62] == 2'h1 & _T_345; // @[FPU.scala 210:40]
  wire  _T_480 = _T_470 & _T_358 | in_in1[63:62] == 2'h2; // @[FPU.scala 211:61]
  wire  _T_482 = in_in1[63:61] == 3'h0; // @[FPU.scala 212:23]
  wire  _T_486 = _T_463 & ~in_in1[61]; // @[FPU.scala 213:27]
  wire [2:0] _T_487 = ~in_in1[63:61]; // @[FPU.scala 214:22]
  wire  _T_489 = _T_487 == 3'h0; // @[FPU.scala 214:22]
  wire  _T_493 = _T_489 & ~in_in1[51]; // @[FPU.scala 215:24]
  wire  _T_495 = _T_489 & in_in1[51]; // @[FPU.scala 216:24]
  wire  _T_497 = ~in_in1[64]; // @[FPU.scala 218:34]
  wire  _T_498 = _T_486 & ~in_in1[64]; // @[FPU.scala 218:31]
  wire  _T_501 = _T_480 & ~in_in1[64]; // @[FPU.scala 218:50]
  wire  _T_504 = _T_472 & _T_497; // @[FPU.scala 219:21]
  wire  _T_507 = _T_482 & _T_497; // @[FPU.scala 219:38]
  wire  _T_508 = _T_482 & in_in1[64]; // @[FPU.scala 219:55]
  wire  _T_509 = _T_472 & in_in1[64]; // @[FPU.scala 220:21]
  wire  _T_510 = _T_480 & in_in1[64]; // @[FPU.scala 220:39]
  wire  _T_511 = _T_486 & in_in1[64]; // @[FPU.scala 220:54]
  wire [9:0] _T_520 = {_T_495,_T_493,_T_498,_T_501,_T_504,_T_507,_T_508,_T_509,_T_510,_T_511}; // @[Cat.scala 30:58]
  wire [9:0] classify_out = in_single ? classify_s : _T_520; // @[FPU.scala 316:10]
  wire [63:0] _T_525 = in_rm[0] ? {{54'd0}, classify_out} : unrec_int; // @[FPU.scala 324:27]
  wire [4:0] _T_529 = in_cmd & 5'hc; // @[FPU.scala 328:16]
  wire [2:0] _T_531 = ~in_rm; // @[FPU.scala 329:27]
  wire [1:0] _T_532 = {dcmp_io_lt,dcmp_io_eq}; // @[Cat.scala 30:58]
  wire [2:0] _GEN_33 = {{1'd0}, _T_532}; // @[FPU.scala 329:34]
  wire [2:0] _T_533 = _T_531 & _GEN_33; // @[FPU.scala 329:34]
  wire [63:0] _GEN_23 = 5'h4 == _T_529 ? {{63'd0}, _T_533 != 3'h0} : _T_525; // @[FPU.scala 328:30 FPU.scala 329:23 FPU.scala 324:21]
  wire [4:0] _GEN_24 = 5'h4 == _T_529 ? dcmp_io_exceptionFlags : 5'h0; // @[FPU.scala 328:30 FPU.scala 330:21 FPU.scala 326:19]
  wire [31:0] _T_549 = RecFNToIN_io_out[31] ? 32'hffffffff : 32'h0; // @[Bitwise.scala 71:12]
  wire [63:0] _T_550 = {_T_549,RecFNToIN_io_out}; // @[Cat.scala 30:58]
  wire  _T_553 = RecFNToIN_io_intExceptionFlags[2:1] != 2'h0; // @[FPU.scala 342:64]
  wire [4:0] _T_557 = {_T_553,3'h0,RecFNToIN_io_intExceptionFlags[0]}; // @[Cat.scala 30:58]
  wire [63:0] _GEN_25 = ~in_typ[1] ? _T_550 : _GEN_23; // @[FPU.scala 340:51 FPU.scala 341:27]
  wire [4:0] _GEN_26 = ~in_typ[1] ? _T_557 : _GEN_24; // @[FPU.scala 340:51 FPU.scala 342:25]
  wire  _T_565 = RecFNToIN_1_io_intExceptionFlags[2:1] != 2'h0; // @[FPU.scala 342:64]
  wire [4:0] _T_569 = {_T_565,3'h0,RecFNToIN_1_io_intExceptionFlags[0]}; // @[Cat.scala 30:58]
  wire [63:0] _GEN_27 = in_typ[1] ? RecFNToIN_1_io_out : _GEN_25; // @[FPU.scala 340:51 FPU.scala 341:27]
  wire [4:0] _GEN_28 = in_typ[1] ? _T_569 : _GEN_26; // @[FPU.scala 340:51 FPU.scala 342:25]
  CompareRecFN dcmp ( // @[FPU.scala 319:20]
    .io_a(dcmp_io_a),
    .io_b(dcmp_io_b),
    .io_signaling(dcmp_io_signaling),
    .io_lt(dcmp_io_lt),
    .io_eq(dcmp_io_eq),
    .io_exceptionFlags(dcmp_io_exceptionFlags)
  );
  RecFNToIN RecFNToIN ( // @[FPU.scala 336:24]
    .io_in(RecFNToIN_io_in),
    .io_roundingMode(RecFNToIN_io_roundingMode),
    .io_signedOut(RecFNToIN_io_signedOut),
    .io_out(RecFNToIN_io_out),
    .io_intExceptionFlags(RecFNToIN_io_intExceptionFlags)
  );
  RecFNToIN_1 RecFNToIN_1 ( // @[FPU.scala 336:24]
    .io_in(RecFNToIN_1_io_in),
    .io_roundingMode(RecFNToIN_1_io_roundingMode),
    .io_signedOut(RecFNToIN_1_io_signedOut),
    .io_out(RecFNToIN_1_io_out),
    .io_intExceptionFlags(RecFNToIN_1_io_intExceptionFlags)
  );
  assign io_as_double_rm = in_rm; // @[FPU.scala 349:16]
  assign io_as_double_in1 = in_in1; // @[FPU.scala 349:16]
  assign io_as_double_in2 = in_in2; // @[FPU.scala 349:16]
  assign io_out_valid = valid; // @[FPU.scala 347:16]
  assign io_out_bits_lt = dcmp_io_lt; // @[FPU.scala 348:18]
  assign io_out_bits_store = in_single ? unrec_s : _T_393; // @[FPU.scala 304:10]
  assign io_out_bits_toint = 5'h8 == _T_529 ? _GEN_27 : _GEN_23; // @[FPU.scala 332:33]
  assign io_out_bits_exc = 5'h8 == _T_529 ? _GEN_28 : _GEN_24; // @[FPU.scala 332:33]
  assign dcmp_io_a = in_in1; // @[FPU.scala 320:13]
  assign dcmp_io_b = in_in2; // @[FPU.scala 321:13]
  assign dcmp_io_signaling = ~in_rm[1]; // @[FPU.scala 322:24]
  assign RecFNToIN_io_in = in_in1; // @[FPU.scala 337:18]
  assign RecFNToIN_io_roundingMode = in_rm[1:0]; // @[FPU.scala 338:28]
  assign RecFNToIN_io_signedOut = ~in_typ[0]; // @[FPU.scala 339:28]
  assign RecFNToIN_1_io_in = in_in1; // @[FPU.scala 337:18]
  assign RecFNToIN_1_io_roundingMode = in_rm[1:0]; // @[FPU.scala 338:28]
  assign RecFNToIN_1_io_signedOut = ~in_typ[0]; // @[FPU.scala 339:28]
  always @(posedge clock) begin
    if (io_in_valid) begin // @[FPU.scala 291:22]
      in_cmd <= io_in_bits_cmd; // @[FPU.scala 292:8]
    end
    if (io_in_valid) begin // @[FPU.scala 291:22]
      in_single <= io_in_bits_single; // @[FPU.scala 292:8]
    end
    if (io_in_valid) begin // @[FPU.scala 291:22]
      in_rm <= io_in_bits_rm; // @[FPU.scala 292:8]
    end
    if (io_in_valid) begin // @[FPU.scala 291:22]
      in_typ <= io_in_bits_typ; // @[FPU.scala 292:8]
    end
    if (io_in_valid) begin // @[FPU.scala 291:22]
      if (io_in_bits_single & ~io_in_bits_ldst & ~(5'hc == _T_228)) begin // @[FPU.scala 293:98]
        in_in1 <= _T_256; // @[FPU.scala 294:14]
      end else begin
        in_in1 <= io_in_bits_in1; // @[FPU.scala 292:8]
      end
    end
    if (io_in_valid) begin // @[FPU.scala 291:22]
      if (io_in_bits_single & ~io_in_bits_ldst & ~(5'hc == _T_228)) begin // @[FPU.scala 293:98]
        in_in2 <= _T_280; // @[FPU.scala 295:14]
      end else begin
        in_in2 <= io_in_bits_in2; // @[FPU.scala 292:8]
      end
    end
    valid <= io_in_valid; // @[FPU.scala 287:18]
  end
// Register and memory initialization
`ifdef RANDOMIZE_GARBAGE_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_INVALID_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_REG_INIT
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_MEM_INIT
`define RANDOMIZE
`endif
`ifndef RANDOM
`define RANDOM $random
`endif
`ifdef RANDOMIZE_MEM_INIT
  integer initvar;
`endif
`ifndef SYNTHESIS
`ifdef FIRRTL_BEFORE_INITIAL
`FIRRTL_BEFORE_INITIAL
`endif
initial begin
  `ifdef RANDOMIZE
    `ifdef INIT_RANDOM
      `INIT_RANDOM
    `endif
    `ifndef VERILATOR
      `ifdef RANDOMIZE_DELAY
        #`RANDOMIZE_DELAY begin end
      `else
        #0.002 begin end
      `endif
    `endif
`ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  in_cmd = _RAND_0[4:0];
  _RAND_1 = {1{`RANDOM}};
  in_single = _RAND_1[0:0];
  _RAND_2 = {1{`RANDOM}};
  in_rm = _RAND_2[2:0];
  _RAND_3 = {1{`RANDOM}};
  in_typ = _RAND_3[1:0];
  _RAND_4 = {3{`RANDOM}};
  in_in1 = _RAND_4[64:0];
  _RAND_5 = {3{`RANDOM}};
  in_in2 = _RAND_5[64:0];
  _RAND_6 = {1{`RANDOM}};
  valid = _RAND_6[0:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module IntToFP(
  input         clock,
  input         reset,
  input         io_in_valid,
  input  [4:0]  io_in_bits_cmd,
  input         io_in_bits_single,
  input  [2:0]  io_in_bits_rm,
  input  [1:0]  io_in_bits_typ,
  input  [64:0] io_in_bits_in1,
  output [64:0] io_out_bits_data,
  output [4:0]  io_out_bits_exc
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_3;
  reg [31:0] _RAND_4;
  reg [95:0] _RAND_5;
  reg [95:0] _RAND_6;
  reg [31:0] _RAND_7;
`endif // RANDOMIZE_REG_INIT
  wire  INToRecFN_io_signedIn; // @[FPU.scala 381:21]
  wire [63:0] INToRecFN_io_in; // @[FPU.scala 381:21]
  wire [1:0] INToRecFN_io_roundingMode; // @[FPU.scala 381:21]
  wire [32:0] INToRecFN_io_out; // @[FPU.scala 381:21]
  wire [4:0] INToRecFN_io_exceptionFlags; // @[FPU.scala 381:21]
  wire  INToRecFN_1_io_signedIn; // @[FPU.scala 391:25]
  wire [63:0] INToRecFN_1_io_in; // @[FPU.scala 391:25]
  wire [1:0] INToRecFN_1_io_roundingMode; // @[FPU.scala 391:25]
  wire [64:0] INToRecFN_1_io_out; // @[FPU.scala 391:25]
  wire [4:0] INToRecFN_1_io_exceptionFlags; // @[FPU.scala 391:25]
  reg  in_valid; // @[Valid.scala 47:18]
  reg [4:0] in_bits_cmd; // @[Reg.scala 34:16]
  reg  in_bits_single; // @[Reg.scala 34:16]
  reg [2:0] in_bits_rm; // @[Reg.scala 34:16]
  reg [1:0] in_bits_typ; // @[Reg.scala 34:16]
  reg [64:0] in_bits_in1; // @[Reg.scala 34:16]
  wire  _T_280 = in_bits_in1[30:23] == 8'h0; // @[recFNFromFN.scala 51:34]
  wire  _T_282 = in_bits_in1[22:0] == 23'h0; // @[recFNFromFN.scala 52:38]
  wire  _T_283 = _T_280 & _T_282; // @[recFNFromFN.scala 53:34]
  wire [31:0] _T_284 = {in_bits_in1[22:0], 9'h0}; // @[recFNFromFN.scala 56:26]
  wire  _T_288 = _T_284[31:16] != 16'h0; // @[CircuitMath.scala 37:22]
  wire  _T_292 = _T_284[31:24] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_296 = _T_284[31:28] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_302 = _T_284[30] ? 2'h2 : {{1'd0}, _T_284[29]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_303 = _T_284[31] ? 2'h3 : _T_302; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_309 = _T_284[26] ? 2'h2 : {{1'd0}, _T_284[25]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_310 = _T_284[27] ? 2'h3 : _T_309; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_311 = _T_296 ? _T_303 : _T_310; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_312 = {_T_296,_T_311}; // @[Cat.scala 30:58]
  wire  _T_316 = _T_284[23:20] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_322 = _T_284[22] ? 2'h2 : {{1'd0}, _T_284[21]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_323 = _T_284[23] ? 2'h3 : _T_322; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_329 = _T_284[18] ? 2'h2 : {{1'd0}, _T_284[17]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_330 = _T_284[19] ? 2'h3 : _T_329; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_331 = _T_316 ? _T_323 : _T_330; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_332 = {_T_316,_T_331}; // @[Cat.scala 30:58]
  wire [2:0] _T_333 = _T_292 ? _T_312 : _T_332; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_334 = {_T_292,_T_333}; // @[Cat.scala 30:58]
  wire  _T_338 = _T_284[15:8] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_342 = _T_284[15:12] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_348 = _T_284[14] ? 2'h2 : {{1'd0}, _T_284[13]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_349 = _T_284[15] ? 2'h3 : _T_348; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_355 = _T_284[10] ? 2'h2 : {{1'd0}, _T_284[9]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_356 = _T_284[11] ? 2'h3 : _T_355; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_357 = _T_342 ? _T_349 : _T_356; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_358 = {_T_342,_T_357}; // @[Cat.scala 30:58]
  wire  _T_362 = _T_284[7:4] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_368 = _T_284[6] ? 2'h2 : {{1'd0}, _T_284[5]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_369 = _T_284[7] ? 2'h3 : _T_368; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_375 = _T_284[2] ? 2'h2 : {{1'd0}, _T_284[1]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_376 = _T_284[3] ? 2'h3 : _T_375; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_377 = _T_362 ? _T_369 : _T_376; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_378 = {_T_362,_T_377}; // @[Cat.scala 30:58]
  wire [2:0] _T_379 = _T_338 ? _T_358 : _T_378; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_380 = {_T_338,_T_379}; // @[Cat.scala 30:58]
  wire [3:0] _T_381 = _T_288 ? _T_334 : _T_380; // @[CircuitMath.scala 38:21]
  wire [4:0] _T_382 = {_T_288,_T_381}; // @[Cat.scala 30:58]
  wire [4:0] _T_383 = ~_T_382; // @[recFNFromFN.scala 56:13]
  wire [53:0] _GEN_29 = {{31'd0}, in_bits_in1[22:0]}; // @[recFNFromFN.scala 58:25]
  wire [53:0] _T_384 = _GEN_29 << _T_383; // @[recFNFromFN.scala 58:25]
  wire [22:0] _T_387 = {_T_384[21:0],1'h0}; // @[Cat.scala 30:58]
  wire [8:0] _GEN_30 = {{4'd0}, _T_383}; // @[recFNFromFN.scala 62:27]
  wire [8:0] _T_393 = _GEN_30 ^ 9'h1ff; // @[recFNFromFN.scala 62:27]
  wire [8:0] _T_394 = _T_280 ? _T_393 : {{1'd0}, in_bits_in1[30:23]}; // @[recFNFromFN.scala 61:16]
  wire [1:0] _T_398 = _T_280 ? 2'h2 : 2'h1; // @[recFNFromFN.scala 64:47]
  wire [7:0] _GEN_31 = {{6'd0}, _T_398}; // @[recFNFromFN.scala 64:42]
  wire [7:0] _T_399 = 8'h80 | _GEN_31; // @[recFNFromFN.scala 64:42]
  wire [8:0] _GEN_32 = {{1'd0}, _T_399}; // @[recFNFromFN.scala 64:15]
  wire [8:0] _T_401 = _T_394 + _GEN_32; // @[recFNFromFN.scala 64:15]
  wire  _T_406 = ~_T_282; // @[recFNFromFN.scala 68:17]
  wire  _T_407 = _T_401[8:7] == 2'h3 & _T_406; // @[recFNFromFN.scala 67:63]
  wire [2:0] _T_411 = _T_283 ? 3'h7 : 3'h0; // @[Bitwise.scala 71:12]
  wire [8:0] _T_412 = {_T_411, 6'h0}; // @[recFNFromFN.scala 71:45]
  wire [8:0] _T_413 = ~_T_412; // @[recFNFromFN.scala 71:28]
  wire [8:0] _T_414 = _T_401 & _T_413; // @[recFNFromFN.scala 71:26]
  wire [6:0] _T_415 = {_T_407, 6'h0}; // @[recFNFromFN.scala 72:22]
  wire [8:0] _GEN_33 = {{2'd0}, _T_415}; // @[recFNFromFN.scala 71:64]
  wire [8:0] _T_416 = _T_414 | _GEN_33; // @[recFNFromFN.scala 71:64]
  wire [22:0] _T_417 = _T_280 ? _T_387 : in_bits_in1[22:0]; // @[recFNFromFN.scala 73:27]
  wire [32:0] _T_419 = {in_bits_in1[31],_T_416,_T_417}; // @[Cat.scala 30:58]
  wire  _T_421 = ~in_bits_single; // @[FPU.scala 363:24]
  wire  _T_426 = in_bits_in1[62:52] == 11'h0; // @[recFNFromFN.scala 51:34]
  wire  _T_428 = in_bits_in1[51:0] == 52'h0; // @[recFNFromFN.scala 52:38]
  wire  _T_429 = _T_426 & _T_428; // @[recFNFromFN.scala 53:34]
  wire [63:0] _T_430 = {in_bits_in1[51:0], 12'h0}; // @[recFNFromFN.scala 56:26]
  wire  _T_434 = _T_430[63:32] != 32'h0; // @[CircuitMath.scala 37:22]
  wire  _T_438 = _T_430[63:48] != 16'h0; // @[CircuitMath.scala 37:22]
  wire  _T_442 = _T_430[63:56] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_446 = _T_430[63:60] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_452 = _T_430[62] ? 2'h2 : {{1'd0}, _T_430[61]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_453 = _T_430[63] ? 2'h3 : _T_452; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_459 = _T_430[58] ? 2'h2 : {{1'd0}, _T_430[57]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_460 = _T_430[59] ? 2'h3 : _T_459; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_461 = _T_446 ? _T_453 : _T_460; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_462 = {_T_446,_T_461}; // @[Cat.scala 30:58]
  wire  _T_466 = _T_430[55:52] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_472 = _T_430[54] ? 2'h2 : {{1'd0}, _T_430[53]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_473 = _T_430[55] ? 2'h3 : _T_472; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_479 = _T_430[50] ? 2'h2 : {{1'd0}, _T_430[49]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_480 = _T_430[51] ? 2'h3 : _T_479; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_481 = _T_466 ? _T_473 : _T_480; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_482 = {_T_466,_T_481}; // @[Cat.scala 30:58]
  wire [2:0] _T_483 = _T_442 ? _T_462 : _T_482; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_484 = {_T_442,_T_483}; // @[Cat.scala 30:58]
  wire  _T_488 = _T_430[47:40] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_492 = _T_430[47:44] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_498 = _T_430[46] ? 2'h2 : {{1'd0}, _T_430[45]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_499 = _T_430[47] ? 2'h3 : _T_498; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_505 = _T_430[42] ? 2'h2 : {{1'd0}, _T_430[41]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_506 = _T_430[43] ? 2'h3 : _T_505; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_507 = _T_492 ? _T_499 : _T_506; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_508 = {_T_492,_T_507}; // @[Cat.scala 30:58]
  wire  _T_512 = _T_430[39:36] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_518 = _T_430[38] ? 2'h2 : {{1'd0}, _T_430[37]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_519 = _T_430[39] ? 2'h3 : _T_518; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_525 = _T_430[34] ? 2'h2 : {{1'd0}, _T_430[33]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_526 = _T_430[35] ? 2'h3 : _T_525; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_527 = _T_512 ? _T_519 : _T_526; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_528 = {_T_512,_T_527}; // @[Cat.scala 30:58]
  wire [2:0] _T_529 = _T_488 ? _T_508 : _T_528; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_530 = {_T_488,_T_529}; // @[Cat.scala 30:58]
  wire [3:0] _T_531 = _T_438 ? _T_484 : _T_530; // @[CircuitMath.scala 38:21]
  wire [4:0] _T_532 = {_T_438,_T_531}; // @[Cat.scala 30:58]
  wire  _T_536 = _T_430[31:16] != 16'h0; // @[CircuitMath.scala 37:22]
  wire  _T_540 = _T_430[31:24] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_544 = _T_430[31:28] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_550 = _T_430[30] ? 2'h2 : {{1'd0}, _T_430[29]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_551 = _T_430[31] ? 2'h3 : _T_550; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_557 = _T_430[26] ? 2'h2 : {{1'd0}, _T_430[25]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_558 = _T_430[27] ? 2'h3 : _T_557; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_559 = _T_544 ? _T_551 : _T_558; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_560 = {_T_544,_T_559}; // @[Cat.scala 30:58]
  wire  _T_564 = _T_430[23:20] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_570 = _T_430[22] ? 2'h2 : {{1'd0}, _T_430[21]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_571 = _T_430[23] ? 2'h3 : _T_570; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_577 = _T_430[18] ? 2'h2 : {{1'd0}, _T_430[17]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_578 = _T_430[19] ? 2'h3 : _T_577; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_579 = _T_564 ? _T_571 : _T_578; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_580 = {_T_564,_T_579}; // @[Cat.scala 30:58]
  wire [2:0] _T_581 = _T_540 ? _T_560 : _T_580; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_582 = {_T_540,_T_581}; // @[Cat.scala 30:58]
  wire  _T_586 = _T_430[15:8] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_590 = _T_430[15:12] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_596 = _T_430[14] ? 2'h2 : {{1'd0}, _T_430[13]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_597 = _T_430[15] ? 2'h3 : _T_596; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_603 = _T_430[10] ? 2'h2 : {{1'd0}, _T_430[9]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_604 = _T_430[11] ? 2'h3 : _T_603; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_605 = _T_590 ? _T_597 : _T_604; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_606 = {_T_590,_T_605}; // @[Cat.scala 30:58]
  wire  _T_610 = _T_430[7:4] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_616 = _T_430[6] ? 2'h2 : {{1'd0}, _T_430[5]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_617 = _T_430[7] ? 2'h3 : _T_616; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_623 = _T_430[2] ? 2'h2 : {{1'd0}, _T_430[1]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_624 = _T_430[3] ? 2'h3 : _T_623; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_625 = _T_610 ? _T_617 : _T_624; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_626 = {_T_610,_T_625}; // @[Cat.scala 30:58]
  wire [2:0] _T_627 = _T_586 ? _T_606 : _T_626; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_628 = {_T_586,_T_627}; // @[Cat.scala 30:58]
  wire [3:0] _T_629 = _T_536 ? _T_582 : _T_628; // @[CircuitMath.scala 38:21]
  wire [4:0] _T_630 = {_T_536,_T_629}; // @[Cat.scala 30:58]
  wire [4:0] _T_631 = _T_434 ? _T_532 : _T_630; // @[CircuitMath.scala 38:21]
  wire [5:0] _T_632 = {_T_434,_T_631}; // @[Cat.scala 30:58]
  wire [5:0] _T_633 = ~_T_632; // @[recFNFromFN.scala 56:13]
  wire [114:0] _GEN_34 = {{63'd0}, in_bits_in1[51:0]}; // @[recFNFromFN.scala 58:25]
  wire [114:0] _T_634 = _GEN_34 << _T_633; // @[recFNFromFN.scala 58:25]
  wire [51:0] _T_637 = {_T_634[50:0],1'h0}; // @[Cat.scala 30:58]
  wire [11:0] _GEN_35 = {{6'd0}, _T_633}; // @[recFNFromFN.scala 62:27]
  wire [11:0] _T_643 = _GEN_35 ^ 12'hfff; // @[recFNFromFN.scala 62:27]
  wire [11:0] _T_644 = _T_426 ? _T_643 : {{1'd0}, in_bits_in1[62:52]}; // @[recFNFromFN.scala 61:16]
  wire [1:0] _T_648 = _T_426 ? 2'h2 : 2'h1; // @[recFNFromFN.scala 64:47]
  wire [10:0] _GEN_36 = {{9'd0}, _T_648}; // @[recFNFromFN.scala 64:42]
  wire [10:0] _T_649 = 11'h400 | _GEN_36; // @[recFNFromFN.scala 64:42]
  wire [11:0] _GEN_37 = {{1'd0}, _T_649}; // @[recFNFromFN.scala 64:15]
  wire [11:0] _T_651 = _T_644 + _GEN_37; // @[recFNFromFN.scala 64:15]
  wire  _T_656 = ~_T_428; // @[recFNFromFN.scala 68:17]
  wire  _T_657 = _T_651[11:10] == 2'h3 & _T_656; // @[recFNFromFN.scala 67:63]
  wire [2:0] _T_661 = _T_429 ? 3'h7 : 3'h0; // @[Bitwise.scala 71:12]
  wire [11:0] _T_662 = {_T_661, 9'h0}; // @[recFNFromFN.scala 71:45]
  wire [11:0] _T_663 = ~_T_662; // @[recFNFromFN.scala 71:28]
  wire [11:0] _T_664 = _T_651 & _T_663; // @[recFNFromFN.scala 71:26]
  wire [9:0] _T_665 = {_T_657, 9'h0}; // @[recFNFromFN.scala 72:22]
  wire [11:0] _GEN_38 = {{2'd0}, _T_665}; // @[recFNFromFN.scala 71:64]
  wire [11:0] _T_666 = _T_664 | _GEN_38; // @[recFNFromFN.scala 71:64]
  wire [51:0] _T_667 = _T_426 ? _T_637 : in_bits_in1[51:0]; // @[recFNFromFN.scala 73:27]
  wire [64:0] _T_669 = {in_bits_in1[63],_T_666,_T_667}; // @[Cat.scala 30:58]
  wire [32:0] _T_677 = {1'b0,$signed(in_bits_in1[31:0])}; // @[FPU.scala 374:45]
  wire [31:0] _T_678 = in_bits_in1[31:0]; // @[FPU.scala 374:60]
  wire [32:0] _T_679 = in_bits_typ[0] ? $signed(_T_677) : $signed({{1{_T_678[31]}},_T_678}); // @[FPU.scala 374:19]
  wire [64:0] intValue = ~in_bits_typ[1] ? $signed({{32{_T_679[32]}},_T_679}) : $signed(in_bits_in1); // @[FPU.scala 377:9]
  wire [4:0] _T_682 = in_bits_cmd & 5'h4; // @[FPU.scala 380:21]
  wire [64:0] _T_689 = {INToRecFN_1_io_out[64:33],INToRecFN_io_out}; // @[Cat.scala 30:58]
  reg [64:0] _T_698_data; // @[Reg.scala 34:16]
  reg [4:0] _T_698_exc; // @[Reg.scala 34:16]
  INToRecFN INToRecFN ( // @[FPU.scala 381:21]
    .io_signedIn(INToRecFN_io_signedIn),
    .io_in(INToRecFN_io_in),
    .io_roundingMode(INToRecFN_io_roundingMode),
    .io_out(INToRecFN_io_out),
    .io_exceptionFlags(INToRecFN_io_exceptionFlags)
  );
  INToRecFN_1 INToRecFN_1 ( // @[FPU.scala 391:25]
    .io_signedIn(INToRecFN_1_io_signedIn),
    .io_in(INToRecFN_1_io_in),
    .io_roundingMode(INToRecFN_1_io_roundingMode),
    .io_out(INToRecFN_1_io_out),
    .io_exceptionFlags(INToRecFN_1_io_exceptionFlags)
  );
  assign io_out_bits_data = _T_698_data; // @[Valid.scala 42:21 Valid.scala 44:16]
  assign io_out_bits_exc = _T_698_exc; // @[Valid.scala 42:21 Valid.scala 44:16]
  assign INToRecFN_io_signedIn = ~in_bits_typ[0]; // @[FPU.scala 382:24]
  assign INToRecFN_io_in = intValue[63:0]; // @[FPU.scala 383:15]
  assign INToRecFN_io_roundingMode = in_bits_rm[1:0]; // @[FPU.scala 384:25]
  assign INToRecFN_1_io_signedIn = ~in_bits_typ[0]; // @[FPU.scala 392:28]
  assign INToRecFN_1_io_in = intValue[63:0]; // @[FPU.scala 393:19]
  assign INToRecFN_1_io_roundingMode = in_bits_rm[1:0]; // @[FPU.scala 394:29]
  always @(posedge clock) begin
    if (reset) begin // @[Valid.scala 47:18]
      in_valid <= 1'h0; // @[Valid.scala 47:18]
    end else begin
      in_valid <= io_in_valid; // @[Valid.scala 47:18]
    end
    if (io_in_valid) begin // @[Reg.scala 35:19]
      in_bits_cmd <= io_in_bits_cmd; // @[Reg.scala 35:23]
    end
    if (io_in_valid) begin // @[Reg.scala 35:19]
      in_bits_single <= io_in_bits_single; // @[Reg.scala 35:23]
    end
    if (io_in_valid) begin // @[Reg.scala 35:19]
      in_bits_rm <= io_in_bits_rm; // @[Reg.scala 35:23]
    end
    if (io_in_valid) begin // @[Reg.scala 35:19]
      in_bits_typ <= io_in_bits_typ; // @[Reg.scala 35:23]
    end
    if (io_in_valid) begin // @[Reg.scala 35:19]
      in_bits_in1 <= io_in_bits_in1; // @[Reg.scala 35:23]
    end
    if (in_valid) begin // @[Reg.scala 35:19]
      if (5'h0 == _T_682) begin // @[FPU.scala 380:38]
        if (_T_421) begin // @[FPU.scala 398:32]
          _T_698_data <= INToRecFN_1_io_out; // @[FPU.scala 399:20]
        end else begin
          _T_698_data <= _T_689; // @[FPU.scala 396:18]
        end
      end else if (~in_bits_single) begin // @[FPU.scala 363:41]
        _T_698_data <= _T_669; // @[FPU.scala 364:14]
      end else begin
        _T_698_data <= {{32'd0}, _T_419}; // @[FPU.scala 362:12]
      end
    end
    if (in_valid) begin // @[Reg.scala 35:19]
      if (5'h0 == _T_682) begin // @[FPU.scala 380:38]
        if (_T_421) begin // @[FPU.scala 398:32]
          _T_698_exc <= INToRecFN_1_io_exceptionFlags; // @[FPU.scala 400:19]
        end else begin
          _T_698_exc <= INToRecFN_io_exceptionFlags; // @[FPU.scala 397:17]
        end
      end else begin
        _T_698_exc <= 5'h0; // @[FPU.scala 361:11]
      end
    end
  end
// Register and memory initialization
`ifdef RANDOMIZE_GARBAGE_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_INVALID_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_REG_INIT
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_MEM_INIT
`define RANDOMIZE
`endif
`ifndef RANDOM
`define RANDOM $random
`endif
`ifdef RANDOMIZE_MEM_INIT
  integer initvar;
`endif
`ifndef SYNTHESIS
`ifdef FIRRTL_BEFORE_INITIAL
`FIRRTL_BEFORE_INITIAL
`endif
initial begin
  `ifdef RANDOMIZE
    `ifdef INIT_RANDOM
      `INIT_RANDOM
    `endif
    `ifndef VERILATOR
      `ifdef RANDOMIZE_DELAY
        #`RANDOMIZE_DELAY begin end
      `else
        #0.002 begin end
      `endif
    `endif
`ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  in_valid = _RAND_0[0:0];
  _RAND_1 = {1{`RANDOM}};
  in_bits_cmd = _RAND_1[4:0];
  _RAND_2 = {1{`RANDOM}};
  in_bits_single = _RAND_2[0:0];
  _RAND_3 = {1{`RANDOM}};
  in_bits_rm = _RAND_3[2:0];
  _RAND_4 = {1{`RANDOM}};
  in_bits_typ = _RAND_4[1:0];
  _RAND_5 = {3{`RANDOM}};
  in_bits_in1 = _RAND_5[64:0];
  _RAND_6 = {3{`RANDOM}};
  _T_698_data = _RAND_6[64:0];
  _RAND_7 = {1{`RANDOM}};
  _T_698_exc = _RAND_7[4:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module FPToFP(
  input         clock,
  input         reset,
  input         io_in_valid,
  input  [4:0]  io_in_bits_cmd,
  input         io_in_bits_single,
  input  [2:0]  io_in_bits_rm,
  input  [64:0] io_in_bits_in1,
  input  [64:0] io_in_bits_in2,
  output [64:0] io_out_bits_data,
  output [4:0]  io_out_bits_exc,
  input         io_lt
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_3;
  reg [95:0] _RAND_4;
  reg [95:0] _RAND_5;
  reg [95:0] _RAND_6;
  reg [31:0] _RAND_7;
`endif // RANDOMIZE_REG_INIT
  wire [64:0] RecFNToRecFN_io_in; // @[FPU.scala 451:25]
  wire [1:0] RecFNToRecFN_io_roundingMode; // @[FPU.scala 451:25]
  wire [32:0] RecFNToRecFN_io_out; // @[FPU.scala 451:25]
  wire [4:0] RecFNToRecFN_io_exceptionFlags; // @[FPU.scala 451:25]
  wire [32:0] RecFNToRecFN_1_io_in; // @[FPU.scala 455:25]
  wire [64:0] RecFNToRecFN_1_io_out; // @[FPU.scala 455:25]
  wire [4:0] RecFNToRecFN_1_io_exceptionFlags; // @[FPU.scala 455:25]
  reg  in_valid; // @[Valid.scala 47:18]
  reg [4:0] in_bits_cmd; // @[Reg.scala 34:16]
  reg  in_bits_single; // @[Reg.scala 34:16]
  reg [2:0] in_bits_rm; // @[Reg.scala 34:16]
  reg [64:0] in_bits_in1; // @[Reg.scala 34:16]
  reg [64:0] in_bits_in2; // @[Reg.scala 34:16]
  wire [64:0] _T_273 = in_bits_in1 ^ in_bits_in2; // @[FPU.scala 417:50]
  wire [64:0] _T_275 = ~in_bits_in2; // @[FPU.scala 417:84]
  wire [64:0] _T_276 = in_bits_rm[0] ? _T_275 : in_bits_in2; // @[FPU.scala 417:68]
  wire [64:0] signNum = in_bits_rm[1] ? _T_273 : _T_276; // @[FPU.scala 417:22]
  wire [64:0] _T_280 = {in_bits_in1[64:33],signNum[32],in_bits_in1[31:0]}; // @[Cat.scala 30:58]
  wire [64:0] _T_283 = {signNum[64],in_bits_in1[63:0]}; // @[Cat.scala 30:58]
  wire [64:0] fsgnj = in_bits_single ? _T_280 : _T_283; // @[FPU.scala 421:21]
  wire [4:0] _T_292 = in_bits_cmd & 5'hd; // @[FPU.scala 428:23]
  wire [2:0] _T_295 = ~in_bits_in1[31:29]; // @[FPU.scala 226:58]
  wire  _T_297 = _T_295 == 3'h0; // @[FPU.scala 226:58]
  wire [2:0] _T_299 = ~in_bits_in2[31:29]; // @[FPU.scala 226:58]
  wire  _T_301 = _T_299 == 3'h0; // @[FPU.scala 226:58]
  wire  _T_309 = _T_297 & ~in_bits_in1[22]; // @[FPU.scala 231:40]
  wire  _T_317 = _T_301 & ~in_bits_in2[22]; // @[FPU.scala 231:40]
  wire  _T_318 = _T_309 | _T_317; // @[FPU.scala 434:31]
  wire  _T_320 = _T_318 | _T_297 & _T_301; // @[FPU.scala 435:32]
  wire  _T_330 = _T_301 | in_bits_rm[0] != io_lt & ~_T_297; // @[FPU.scala 437:17]
  wire [2:0] _T_332 = ~in_bits_in1[63:61]; // @[FPU.scala 226:58]
  wire  _T_334 = _T_332 == 3'h0; // @[FPU.scala 226:58]
  wire [2:0] _T_336 = ~in_bits_in2[63:61]; // @[FPU.scala 226:58]
  wire  _T_338 = _T_336 == 3'h0; // @[FPU.scala 226:58]
  wire  _T_346 = _T_334 & ~in_bits_in1[51]; // @[FPU.scala 231:40]
  wire  _T_354 = _T_338 & ~in_bits_in2[51]; // @[FPU.scala 231:40]
  wire  _T_355 = _T_346 | _T_354; // @[FPU.scala 434:31]
  wire  _T_357 = _T_355 | _T_334 & _T_338; // @[FPU.scala 435:32]
  wire  _T_364 = _T_338 | in_bits_rm[0] != io_lt & ~_T_334; // @[FPU.scala 437:17]
  wire  _T_365 = in_bits_single ? _T_330 : _T_364; // @[Misc.scala 42:9]
  wire  _T_366 = in_bits_single ? _T_318 : _T_355; // @[Misc.scala 42:36]
  wire  _T_367 = in_bits_single ? _T_320 : _T_357; // @[Misc.scala 42:63]
  wire [64:0] _T_368 = in_bits_single ? 65'he0080000e0400000 : 65'he008000000000000; // @[Misc.scala 42:90]
  wire [4:0] _T_369 = {_T_366, 4'h0}; // @[FPU.scala 443:28]
  wire [64:0] _T_370 = _T_365 ? in_bits_in1 : in_bits_in2; // @[FPU.scala 444:42]
  wire [64:0] _T_371 = _T_367 ? _T_368 : _T_370; // @[FPU.scala 444:22]
  wire [4:0] _GEN_21 = 5'h5 == _T_292 ? _T_369 : 5'h0; // @[FPU.scala 428:40 FPU.scala 443:15 FPU.scala 425:13]
  wire [64:0] _GEN_22 = 5'h5 == _T_292 ? _T_371 : fsgnj; // @[FPU.scala 428:40 FPU.scala 444:16 FPU.scala 426:14]
  wire [4:0] _T_374 = in_bits_cmd & 5'h4; // @[FPU.scala 450:25]
  wire [64:0] _T_377 = {RecFNToRecFN_1_io_out[64:33],RecFNToRecFN_io_out}; // @[Cat.scala 30:58]
  wire  _T_379 = ~in_bits_single; // @[FPU.scala 459:31]
  reg [64:0] _T_386_data; // @[Reg.scala 34:16]
  reg [4:0] _T_386_exc; // @[Reg.scala 34:16]
  RecFNToRecFN_2 RecFNToRecFN ( // @[FPU.scala 451:25]
    .io_in(RecFNToRecFN_io_in),
    .io_roundingMode(RecFNToRecFN_io_roundingMode),
    .io_out(RecFNToRecFN_io_out),
    .io_exceptionFlags(RecFNToRecFN_io_exceptionFlags)
  );
  RecFNToRecFN_1 RecFNToRecFN_1 ( // @[FPU.scala 455:25]
    .io_in(RecFNToRecFN_1_io_in),
    .io_out(RecFNToRecFN_1_io_out),
    .io_exceptionFlags(RecFNToRecFN_1_io_exceptionFlags)
  );
  assign io_out_bits_data = _T_386_data; // @[Valid.scala 42:21 Valid.scala 44:16]
  assign io_out_bits_exc = _T_386_exc; // @[Valid.scala 42:21 Valid.scala 44:16]
  assign RecFNToRecFN_io_in = in_bits_in1; // @[Valid.scala 42:21 Valid.scala 44:16]
  assign RecFNToRecFN_io_roundingMode = in_bits_rm[1:0]; // @[FPU.scala 453:29]
  assign RecFNToRecFN_1_io_in = in_bits_in1[32:0]; // @[FPU.scala 456:19]
  always @(posedge clock) begin
    if (reset) begin // @[Valid.scala 47:18]
      in_valid <= 1'h0; // @[Valid.scala 47:18]
    end else begin
      in_valid <= io_in_valid; // @[Valid.scala 47:18]
    end
    if (io_in_valid) begin // @[Reg.scala 35:19]
      in_bits_cmd <= io_in_bits_cmd; // @[Reg.scala 35:23]
    end
    if (io_in_valid) begin // @[Reg.scala 35:19]
      in_bits_single <= io_in_bits_single; // @[Reg.scala 35:23]
    end
    if (io_in_valid) begin // @[Reg.scala 35:19]
      in_bits_rm <= io_in_bits_rm; // @[Reg.scala 35:23]
    end
    if (io_in_valid) begin // @[Reg.scala 35:19]
      in_bits_in1 <= io_in_bits_in1; // @[Reg.scala 35:23]
    end
    if (io_in_valid) begin // @[Reg.scala 35:19]
      in_bits_in2 <= io_in_bits_in2; // @[Reg.scala 35:23]
    end
    if (in_valid) begin // @[Reg.scala 35:19]
      if (5'h0 == _T_374) begin // @[FPU.scala 450:42]
        if (_T_379) begin // @[FPU.scala 462:21]
          _T_386_data <= RecFNToRecFN_1_io_out; // @[FPU.scala 463:20]
        end else if (in_bits_single) begin // @[FPU.scala 459:31]
          _T_386_data <= _T_377; // @[FPU.scala 460:20]
        end else begin
          _T_386_data <= _GEN_22;
        end
      end else begin
        _T_386_data <= _GEN_22;
      end
    end
    if (in_valid) begin // @[Reg.scala 35:19]
      if (5'h0 == _T_374) begin // @[FPU.scala 450:42]
        if (_T_379) begin // @[FPU.scala 462:21]
          _T_386_exc <= RecFNToRecFN_1_io_exceptionFlags; // @[FPU.scala 464:19]
        end else if (in_bits_single) begin // @[FPU.scala 459:31]
          _T_386_exc <= RecFNToRecFN_io_exceptionFlags; // @[FPU.scala 461:19]
        end else begin
          _T_386_exc <= _GEN_21;
        end
      end else begin
        _T_386_exc <= _GEN_21;
      end
    end
  end
// Register and memory initialization
`ifdef RANDOMIZE_GARBAGE_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_INVALID_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_REG_INIT
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_MEM_INIT
`define RANDOMIZE
`endif
`ifndef RANDOM
`define RANDOM $random
`endif
`ifdef RANDOMIZE_MEM_INIT
  integer initvar;
`endif
`ifndef SYNTHESIS
`ifdef FIRRTL_BEFORE_INITIAL
`FIRRTL_BEFORE_INITIAL
`endif
initial begin
  `ifdef RANDOMIZE
    `ifdef INIT_RANDOM
      `INIT_RANDOM
    `endif
    `ifndef VERILATOR
      `ifdef RANDOMIZE_DELAY
        #`RANDOMIZE_DELAY begin end
      `else
        #0.002 begin end
      `endif
    `endif
`ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  in_valid = _RAND_0[0:0];
  _RAND_1 = {1{`RANDOM}};
  in_bits_cmd = _RAND_1[4:0];
  _RAND_2 = {1{`RANDOM}};
  in_bits_single = _RAND_2[0:0];
  _RAND_3 = {1{`RANDOM}};
  in_bits_rm = _RAND_3[2:0];
  _RAND_4 = {3{`RANDOM}};
  in_bits_in1 = _RAND_4[64:0];
  _RAND_5 = {3{`RANDOM}};
  in_bits_in2 = _RAND_5[64:0];
  _RAND_6 = {3{`RANDOM}};
  _T_386_data = _RAND_6[64:0];
  _RAND_7 = {1{`RANDOM}};
  _T_386_exc = _RAND_7[4:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module FPUFMAPipe_1(
  input         clock,
  input         reset,
  input         io_in_valid,
  input  [4:0]  io_in_bits_cmd,
  input         io_in_bits_ren3,
  input         io_in_bits_swap23,
  input  [2:0]  io_in_bits_rm,
  input  [64:0] io_in_bits_in1,
  input  [64:0] io_in_bits_in2,
  input  [64:0] io_in_bits_in3,
  output [64:0] io_out_bits_data,
  output [4:0]  io_out_bits_exc
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [95:0] _RAND_3;
  reg [95:0] _RAND_4;
  reg [95:0] _RAND_5;
  reg [31:0] _RAND_6;
  reg [95:0] _RAND_7;
  reg [31:0] _RAND_8;
  reg [31:0] _RAND_9;
  reg [95:0] _RAND_10;
  reg [31:0] _RAND_11;
  reg [95:0] _RAND_12;
  reg [31:0] _RAND_13;
`endif // RANDOMIZE_REG_INIT
  wire [1:0] fma_io_op; // @[FPU.scala 493:19]
  wire [64:0] fma_io_a; // @[FPU.scala 493:19]
  wire [64:0] fma_io_b; // @[FPU.scala 493:19]
  wire [64:0] fma_io_c; // @[FPU.scala 493:19]
  wire [1:0] fma_io_roundingMode; // @[FPU.scala 493:19]
  wire [64:0] fma_io_out; // @[FPU.scala 493:19]
  wire [4:0] fma_io_exceptionFlags; // @[FPU.scala 493:19]
  wire  _T_133 = io_in_bits_in1[64] ^ io_in_bits_in2[64]; // @[FPU.scala 480:37]
  wire [64:0] zero = {_T_133, 64'h0}; // @[FPU.scala 480:62]
  reg  valid; // @[FPU.scala 482:18]
  reg [4:0] in_cmd; // @[FPU.scala 483:15]
  reg [2:0] in_rm; // @[FPU.scala 483:15]
  reg [64:0] in_in1; // @[FPU.scala 483:15]
  reg [64:0] in_in2; // @[FPU.scala 483:15]
  reg [64:0] in_in3; // @[FPU.scala 483:15]
  wire  _T_178 = io_in_bits_ren3 | io_in_bits_swap23; // @[FPU.scala 488:48]
  wire  _T_179 = io_in_bits_cmd[1] & (io_in_bits_ren3 | io_in_bits_swap23); // @[FPU.scala 488:37]
  wire [1:0] _T_181 = {_T_179,io_in_bits_cmd[0]}; // @[Cat.scala 30:58]
  reg  _T_192; // @[Valid.scala 47:18]
  reg [64:0] _T_196_data; // @[Reg.scala 34:16]
  reg [4:0] _T_196_exc; // @[Reg.scala 34:16]
  wire [64:0] res_data = fma_io_out; // @[FPU.scala 500:17 FPU.scala 501:12]
  wire [4:0] res_exc = fma_io_exceptionFlags; // @[FPU.scala 500:17 FPU.scala 502:11]
  reg  _T_201; // @[Valid.scala 47:18]
  reg [64:0] _T_205_data; // @[Reg.scala 34:16]
  reg [4:0] _T_205_exc; // @[Reg.scala 34:16]
  reg [64:0] _T_214_data; // @[Reg.scala 34:16]
  reg [4:0] _T_214_exc; // @[Reg.scala 34:16]
  MulAddRecFN_1 fma ( // @[FPU.scala 493:19]
    .io_op(fma_io_op),
    .io_a(fma_io_a),
    .io_b(fma_io_b),
    .io_c(fma_io_c),
    .io_roundingMode(fma_io_roundingMode),
    .io_out(fma_io_out),
    .io_exceptionFlags(fma_io_exceptionFlags)
  );
  assign io_out_bits_data = _T_214_data; // @[Valid.scala 42:21 Valid.scala 44:16]
  assign io_out_bits_exc = _T_214_exc; // @[Valid.scala 42:21 Valid.scala 44:16]
  assign fma_io_op = in_cmd[1:0]; // @[FPU.scala 494:13]
  assign fma_io_a = in_in1; // @[FPU.scala 496:12]
  assign fma_io_b = in_in2; // @[FPU.scala 497:12]
  assign fma_io_c = in_in3; // @[FPU.scala 498:12]
  assign fma_io_roundingMode = in_rm[1:0]; // @[FPU.scala 495:23]
  always @(posedge clock) begin
    valid <= io_in_valid; // @[FPU.scala 482:18]
    if (io_in_valid) begin // @[FPU.scala 484:22]
      in_cmd <= {{3'd0}, _T_181}; // @[FPU.scala 488:12]
    end
    if (io_in_valid) begin // @[FPU.scala 484:22]
      in_rm <= io_in_bits_rm; // @[FPU.scala 485:8]
    end
    if (io_in_valid) begin // @[FPU.scala 484:22]
      in_in1 <= io_in_bits_in1; // @[FPU.scala 485:8]
    end
    if (io_in_valid) begin // @[FPU.scala 484:22]
      if (io_in_bits_swap23) begin // @[FPU.scala 489:23]
        in_in2 <= 65'h8000000000000000; // @[FPU.scala 489:32]
      end else begin
        in_in2 <= io_in_bits_in2; // @[FPU.scala 485:8]
      end
    end
    if (io_in_valid) begin // @[FPU.scala 484:22]
      if (~_T_178) begin // @[Conditional.scala 19:15]
        in_in3 <= zero; // @[FPU.scala 490:45]
      end else begin
        in_in3 <= io_in_bits_in3; // @[FPU.scala 485:8]
      end
    end
    if (reset) begin // @[Valid.scala 47:18]
      _T_192 <= 1'h0; // @[Valid.scala 47:18]
    end else begin
      _T_192 <= valid; // @[Valid.scala 47:18]
    end
    if (valid) begin // @[Reg.scala 35:19]
      _T_196_data <= res_data; // @[Reg.scala 35:23]
    end
    if (valid) begin // @[Reg.scala 35:19]
      _T_196_exc <= res_exc; // @[Reg.scala 35:23]
    end
    if (reset) begin // @[Valid.scala 47:18]
      _T_201 <= 1'h0; // @[Valid.scala 47:18]
    end else begin
      _T_201 <= _T_192; // @[Valid.scala 47:18]
    end
    if (_T_192) begin // @[Reg.scala 35:19]
      _T_205_data <= _T_196_data; // @[Reg.scala 35:23]
    end
    if (_T_192) begin // @[Reg.scala 35:19]
      _T_205_exc <= _T_196_exc; // @[Reg.scala 35:23]
    end
    if (_T_201) begin // @[Reg.scala 35:19]
      _T_214_data <= _T_205_data; // @[Reg.scala 35:23]
    end
    if (_T_201) begin // @[Reg.scala 35:19]
      _T_214_exc <= _T_205_exc; // @[Reg.scala 35:23]
    end
  end
// Register and memory initialization
`ifdef RANDOMIZE_GARBAGE_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_INVALID_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_REG_INIT
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_MEM_INIT
`define RANDOMIZE
`endif
`ifndef RANDOM
`define RANDOM $random
`endif
`ifdef RANDOMIZE_MEM_INIT
  integer initvar;
`endif
`ifndef SYNTHESIS
`ifdef FIRRTL_BEFORE_INITIAL
`FIRRTL_BEFORE_INITIAL
`endif
initial begin
  `ifdef RANDOMIZE
    `ifdef INIT_RANDOM
      `INIT_RANDOM
    `endif
    `ifndef VERILATOR
      `ifdef RANDOMIZE_DELAY
        #`RANDOMIZE_DELAY begin end
      `else
        #0.002 begin end
      `endif
    `endif
`ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  valid = _RAND_0[0:0];
  _RAND_1 = {1{`RANDOM}};
  in_cmd = _RAND_1[4:0];
  _RAND_2 = {1{`RANDOM}};
  in_rm = _RAND_2[2:0];
  _RAND_3 = {3{`RANDOM}};
  in_in1 = _RAND_3[64:0];
  _RAND_4 = {3{`RANDOM}};
  in_in2 = _RAND_4[64:0];
  _RAND_5 = {3{`RANDOM}};
  in_in3 = _RAND_5[64:0];
  _RAND_6 = {1{`RANDOM}};
  _T_192 = _RAND_6[0:0];
  _RAND_7 = {3{`RANDOM}};
  _T_196_data = _RAND_7[64:0];
  _RAND_8 = {1{`RANDOM}};
  _T_196_exc = _RAND_8[4:0];
  _RAND_9 = {1{`RANDOM}};
  _T_201 = _RAND_9[0:0];
  _RAND_10 = {3{`RANDOM}};
  _T_205_data = _RAND_10[64:0];
  _RAND_11 = {1{`RANDOM}};
  _T_205_exc = _RAND_11[4:0];
  _RAND_12 = {3{`RANDOM}};
  _T_214_data = _RAND_12[64:0];
  _RAND_13 = {1{`RANDOM}};
  _T_214_exc = _RAND_13[4:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module DivSqrtRecF64(
  input         clock,
  input         reset,
  output        io_inReady_div,
  output        io_inReady_sqrt,
  input         io_inValid,
  input         io_sqrtOp,
  input  [64:0] io_a,
  input  [64:0] io_b,
  input  [1:0]  io_roundingMode,
  output        io_outValid_div,
  output        io_outValid_sqrt,
  output [64:0] io_out,
  output [4:0]  io_exceptionFlags
);
  wire  ds_clock; // @[DivSqrtRecF64.scala 59:20]
  wire  ds_reset; // @[DivSqrtRecF64.scala 59:20]
  wire  ds_io_inReady_div; // @[DivSqrtRecF64.scala 59:20]
  wire  ds_io_inReady_sqrt; // @[DivSqrtRecF64.scala 59:20]
  wire  ds_io_inValid; // @[DivSqrtRecF64.scala 59:20]
  wire  ds_io_sqrtOp; // @[DivSqrtRecF64.scala 59:20]
  wire [64:0] ds_io_a; // @[DivSqrtRecF64.scala 59:20]
  wire [64:0] ds_io_b; // @[DivSqrtRecF64.scala 59:20]
  wire [1:0] ds_io_roundingMode; // @[DivSqrtRecF64.scala 59:20]
  wire  ds_io_outValid_div; // @[DivSqrtRecF64.scala 59:20]
  wire  ds_io_outValid_sqrt; // @[DivSqrtRecF64.scala 59:20]
  wire [64:0] ds_io_out; // @[DivSqrtRecF64.scala 59:20]
  wire [4:0] ds_io_exceptionFlags; // @[DivSqrtRecF64.scala 59:20]
  wire [3:0] ds_io_usingMulAdd; // @[DivSqrtRecF64.scala 59:20]
  wire  ds_io_latchMulAddA_0; // @[DivSqrtRecF64.scala 59:20]
  wire [53:0] ds_io_mulAddA_0; // @[DivSqrtRecF64.scala 59:20]
  wire  ds_io_latchMulAddB_0; // @[DivSqrtRecF64.scala 59:20]
  wire [53:0] ds_io_mulAddB_0; // @[DivSqrtRecF64.scala 59:20]
  wire [104:0] ds_io_mulAddC_2; // @[DivSqrtRecF64.scala 59:20]
  wire [104:0] ds_io_mulAddResult_3; // @[DivSqrtRecF64.scala 59:20]
  wire  mul_clock; // @[DivSqrtRecF64.scala 73:21]
  wire  mul_io_val_s0; // @[DivSqrtRecF64.scala 73:21]
  wire  mul_io_latch_a_s0; // @[DivSqrtRecF64.scala 73:21]
  wire [53:0] mul_io_a_s0; // @[DivSqrtRecF64.scala 73:21]
  wire  mul_io_latch_b_s0; // @[DivSqrtRecF64.scala 73:21]
  wire [53:0] mul_io_b_s0; // @[DivSqrtRecF64.scala 73:21]
  wire [104:0] mul_io_c_s2; // @[DivSqrtRecF64.scala 73:21]
  wire [104:0] mul_io_result_s3; // @[DivSqrtRecF64.scala 73:21]
  DivSqrtRecF64_mulAddZ31 ds ( // @[DivSqrtRecF64.scala 59:20]
    .clock(ds_clock),
    .reset(ds_reset),
    .io_inReady_div(ds_io_inReady_div),
    .io_inReady_sqrt(ds_io_inReady_sqrt),
    .io_inValid(ds_io_inValid),
    .io_sqrtOp(ds_io_sqrtOp),
    .io_a(ds_io_a),
    .io_b(ds_io_b),
    .io_roundingMode(ds_io_roundingMode),
    .io_outValid_div(ds_io_outValid_div),
    .io_outValid_sqrt(ds_io_outValid_sqrt),
    .io_out(ds_io_out),
    .io_exceptionFlags(ds_io_exceptionFlags),
    .io_usingMulAdd(ds_io_usingMulAdd),
    .io_latchMulAddA_0(ds_io_latchMulAddA_0),
    .io_mulAddA_0(ds_io_mulAddA_0),
    .io_latchMulAddB_0(ds_io_latchMulAddB_0),
    .io_mulAddB_0(ds_io_mulAddB_0),
    .io_mulAddC_2(ds_io_mulAddC_2),
    .io_mulAddResult_3(ds_io_mulAddResult_3)
  );
  Mul54 mul ( // @[DivSqrtRecF64.scala 73:21]
    .clock(mul_clock),
    .io_val_s0(mul_io_val_s0),
    .io_latch_a_s0(mul_io_latch_a_s0),
    .io_a_s0(mul_io_a_s0),
    .io_latch_b_s0(mul_io_latch_b_s0),
    .io_b_s0(mul_io_b_s0),
    .io_c_s2(mul_io_c_s2),
    .io_result_s3(mul_io_result_s3)
  );
  assign io_inReady_div = ds_io_inReady_div; // @[DivSqrtRecF64.scala 61:20]
  assign io_inReady_sqrt = ds_io_inReady_sqrt; // @[DivSqrtRecF64.scala 62:21]
  assign io_outValid_div = ds_io_outValid_div; // @[DivSqrtRecF64.scala 68:21]
  assign io_outValid_sqrt = ds_io_outValid_sqrt; // @[DivSqrtRecF64.scala 69:22]
  assign io_out = ds_io_out; // @[DivSqrtRecF64.scala 70:12]
  assign io_exceptionFlags = ds_io_exceptionFlags; // @[DivSqrtRecF64.scala 71:23]
  assign ds_clock = clock;
  assign ds_reset = reset;
  assign ds_io_inValid = io_inValid; // @[DivSqrtRecF64.scala 63:19]
  assign ds_io_sqrtOp = io_sqrtOp; // @[DivSqrtRecF64.scala 64:18]
  assign ds_io_a = io_a; // @[DivSqrtRecF64.scala 65:13]
  assign ds_io_b = io_b; // @[DivSqrtRecF64.scala 66:13]
  assign ds_io_roundingMode = io_roundingMode; // @[DivSqrtRecF64.scala 67:24]
  assign ds_io_mulAddResult_3 = mul_io_result_s3; // @[DivSqrtRecF64.scala 81:26]
  assign mul_clock = clock;
  assign mul_io_val_s0 = ds_io_usingMulAdd[0]; // @[DivSqrtRecF64.scala 75:39]
  assign mul_io_latch_a_s0 = ds_io_latchMulAddA_0; // @[DivSqrtRecF64.scala 76:23]
  assign mul_io_a_s0 = ds_io_mulAddA_0; // @[DivSqrtRecF64.scala 77:17]
  assign mul_io_latch_b_s0 = ds_io_latchMulAddB_0; // @[DivSqrtRecF64.scala 78:23]
  assign mul_io_b_s0 = ds_io_mulAddB_0; // @[DivSqrtRecF64.scala 79:17]
  assign mul_io_c_s2 = ds_io_mulAddC_2; // @[DivSqrtRecF64.scala 80:17]
endmodule
module RecFNToRecFN_2(
  input  [64:0] io_in,
  input  [1:0]  io_roundingMode,
  output [32:0] io_out,
  output [4:0]  io_exceptionFlags
);
  wire  RoundRawFNToRecFN_io_invalidExc; // @[RecFNToRecFN.scala 102:19]
  wire  RoundRawFNToRecFN_io_in_sign; // @[RecFNToRecFN.scala 102:19]
  wire  RoundRawFNToRecFN_io_in_isNaN; // @[RecFNToRecFN.scala 102:19]
  wire  RoundRawFNToRecFN_io_in_isInf; // @[RecFNToRecFN.scala 102:19]
  wire  RoundRawFNToRecFN_io_in_isZero; // @[RecFNToRecFN.scala 102:19]
  wire [9:0] RoundRawFNToRecFN_io_in_sExp; // @[RecFNToRecFN.scala 102:19]
  wire [26:0] RoundRawFNToRecFN_io_in_sig; // @[RecFNToRecFN.scala 102:19]
  wire [1:0] RoundRawFNToRecFN_io_roundingMode; // @[RecFNToRecFN.scala 102:19]
  wire [32:0] RoundRawFNToRecFN_io_out; // @[RecFNToRecFN.scala 102:19]
  wire [4:0] RoundRawFNToRecFN_io_exceptionFlags; // @[RecFNToRecFN.scala 102:19]
  wire  outRawFloat_isZero = io_in[63:61] == 3'h0; // @[rawFNFromRecFN.scala 51:54]
  wire  _T_16 = io_in[63:62] == 2'h3; // @[rawFNFromRecFN.scala 52:54]
  wire  outRawFloat_isNaN = _T_16 & io_in[61]; // @[rawFNFromRecFN.scala 56:32]
  wire [12:0] _T_38 = {1'b0,$signed(io_in[63:52])}; // @[rawFNFromRecFN.scala 59:25]
  wire  _T_41 = ~outRawFloat_isZero; // @[rawFNFromRecFN.scala 60:36]
  wire [55:0] _T_46 = {1'h0,_T_41,io_in[51:0],2'h0}; // @[Cat.scala 30:58]
  wire [13:0] _T_48 = $signed(_T_38) - 13'sh700; // @[resizeRawFN.scala 49:31]
  wire  _T_63 = $signed(_T_48) < 14'sh0; // @[resizeRawFN.scala 60:31]
  wire [8:0] _T_75 = _T_48[12:9] != 4'h0 ? 9'h1fc : _T_48[8:0]; // @[resizeRawFN.scala 61:25]
  wire  _T_81 = _T_46[29:0] != 30'h0; // @[resizeRawFN.scala 72:56]
  wire [26:0] outRawFloat_sig = {_T_46[55:30],_T_81}; // @[Cat.scala 30:58]
  RoundRawFNToRecFN_1 RoundRawFNToRecFN ( // @[RecFNToRecFN.scala 102:19]
    .io_invalidExc(RoundRawFNToRecFN_io_invalidExc),
    .io_in_sign(RoundRawFNToRecFN_io_in_sign),
    .io_in_isNaN(RoundRawFNToRecFN_io_in_isNaN),
    .io_in_isInf(RoundRawFNToRecFN_io_in_isInf),
    .io_in_isZero(RoundRawFNToRecFN_io_in_isZero),
    .io_in_sExp(RoundRawFNToRecFN_io_in_sExp),
    .io_in_sig(RoundRawFNToRecFN_io_in_sig),
    .io_roundingMode(RoundRawFNToRecFN_io_roundingMode),
    .io_out(RoundRawFNToRecFN_io_out),
    .io_exceptionFlags(RoundRawFNToRecFN_io_exceptionFlags)
  );
  assign io_out = RoundRawFNToRecFN_io_out; // @[RecFNToRecFN.scala 107:16]
  assign io_exceptionFlags = RoundRawFNToRecFN_io_exceptionFlags; // @[RecFNToRecFN.scala 108:27]
  assign RoundRawFNToRecFN_io_invalidExc = outRawFloat_isNaN & ~outRawFloat_sig[24]; // @[RoundRawFNToRecFN.scala 61:46]
  assign RoundRawFNToRecFN_io_in_sign = io_in[64]; // @[rawFNFromRecFN.scala 55:23]
  assign RoundRawFNToRecFN_io_in_isNaN = _T_16 & io_in[61]; // @[rawFNFromRecFN.scala 56:32]
  assign RoundRawFNToRecFN_io_in_isInf = _T_16 & ~io_in[61]; // @[rawFNFromRecFN.scala 57:32]
  assign RoundRawFNToRecFN_io_in_isZero = io_in[63:61] == 3'h0; // @[rawFNFromRecFN.scala 51:54]
  assign RoundRawFNToRecFN_io_in_sExp = {_T_63,_T_75}; // @[resizeRawFN.scala 65:20]
  assign RoundRawFNToRecFN_io_in_sig = {_T_46[55:30],_T_81}; // @[Cat.scala 30:58]
  assign RoundRawFNToRecFN_io_roundingMode = io_roundingMode; // @[RecFNToRecFN.scala 106:43]
endmodule
module MulAddRecFN(
  input  [1:0]  io_op,
  input  [32:0] io_a,
  input  [32:0] io_b,
  input  [32:0] io_c,
  input  [1:0]  io_roundingMode,
  output [32:0] io_out,
  output [4:0]  io_exceptionFlags
);
  wire [1:0] mulAddRecFN_preMul_io_op; // @[MulAddRecFN.scala 598:15]
  wire [32:0] mulAddRecFN_preMul_io_a; // @[MulAddRecFN.scala 598:15]
  wire [32:0] mulAddRecFN_preMul_io_b; // @[MulAddRecFN.scala 598:15]
  wire [32:0] mulAddRecFN_preMul_io_c; // @[MulAddRecFN.scala 598:15]
  wire [1:0] mulAddRecFN_preMul_io_roundingMode; // @[MulAddRecFN.scala 598:15]
  wire [23:0] mulAddRecFN_preMul_io_mulAddA; // @[MulAddRecFN.scala 598:15]
  wire [23:0] mulAddRecFN_preMul_io_mulAddB; // @[MulAddRecFN.scala 598:15]
  wire [47:0] mulAddRecFN_preMul_io_mulAddC; // @[MulAddRecFN.scala 598:15]
  wire [2:0] mulAddRecFN_preMul_io_toPostMul_highExpA; // @[MulAddRecFN.scala 598:15]
  wire  mulAddRecFN_preMul_io_toPostMul_isNaN_isQuietNaNA; // @[MulAddRecFN.scala 598:15]
  wire [2:0] mulAddRecFN_preMul_io_toPostMul_highExpB; // @[MulAddRecFN.scala 598:15]
  wire  mulAddRecFN_preMul_io_toPostMul_isNaN_isQuietNaNB; // @[MulAddRecFN.scala 598:15]
  wire  mulAddRecFN_preMul_io_toPostMul_signProd; // @[MulAddRecFN.scala 598:15]
  wire  mulAddRecFN_preMul_io_toPostMul_isZeroProd; // @[MulAddRecFN.scala 598:15]
  wire  mulAddRecFN_preMul_io_toPostMul_opSignC; // @[MulAddRecFN.scala 598:15]
  wire [2:0] mulAddRecFN_preMul_io_toPostMul_highExpC; // @[MulAddRecFN.scala 598:15]
  wire  mulAddRecFN_preMul_io_toPostMul_isNaN_isQuietNaNC; // @[MulAddRecFN.scala 598:15]
  wire  mulAddRecFN_preMul_io_toPostMul_isCDominant; // @[MulAddRecFN.scala 598:15]
  wire  mulAddRecFN_preMul_io_toPostMul_CAlignDist_0; // @[MulAddRecFN.scala 598:15]
  wire [6:0] mulAddRecFN_preMul_io_toPostMul_CAlignDist; // @[MulAddRecFN.scala 598:15]
  wire  mulAddRecFN_preMul_io_toPostMul_bit0AlignedNegSigC; // @[MulAddRecFN.scala 598:15]
  wire [25:0] mulAddRecFN_preMul_io_toPostMul_highAlignedNegSigC; // @[MulAddRecFN.scala 598:15]
  wire [10:0] mulAddRecFN_preMul_io_toPostMul_sExpSum; // @[MulAddRecFN.scala 598:15]
  wire [1:0] mulAddRecFN_preMul_io_toPostMul_roundingMode; // @[MulAddRecFN.scala 598:15]
  wire [2:0] mulAddRecFN_postMul_io_fromPreMul_highExpA; // @[MulAddRecFN.scala 600:15]
  wire  mulAddRecFN_postMul_io_fromPreMul_isNaN_isQuietNaNA; // @[MulAddRecFN.scala 600:15]
  wire [2:0] mulAddRecFN_postMul_io_fromPreMul_highExpB; // @[MulAddRecFN.scala 600:15]
  wire  mulAddRecFN_postMul_io_fromPreMul_isNaN_isQuietNaNB; // @[MulAddRecFN.scala 600:15]
  wire  mulAddRecFN_postMul_io_fromPreMul_signProd; // @[MulAddRecFN.scala 600:15]
  wire  mulAddRecFN_postMul_io_fromPreMul_isZeroProd; // @[MulAddRecFN.scala 600:15]
  wire  mulAddRecFN_postMul_io_fromPreMul_opSignC; // @[MulAddRecFN.scala 600:15]
  wire [2:0] mulAddRecFN_postMul_io_fromPreMul_highExpC; // @[MulAddRecFN.scala 600:15]
  wire  mulAddRecFN_postMul_io_fromPreMul_isNaN_isQuietNaNC; // @[MulAddRecFN.scala 600:15]
  wire  mulAddRecFN_postMul_io_fromPreMul_isCDominant; // @[MulAddRecFN.scala 600:15]
  wire  mulAddRecFN_postMul_io_fromPreMul_CAlignDist_0; // @[MulAddRecFN.scala 600:15]
  wire [6:0] mulAddRecFN_postMul_io_fromPreMul_CAlignDist; // @[MulAddRecFN.scala 600:15]
  wire  mulAddRecFN_postMul_io_fromPreMul_bit0AlignedNegSigC; // @[MulAddRecFN.scala 600:15]
  wire [25:0] mulAddRecFN_postMul_io_fromPreMul_highAlignedNegSigC; // @[MulAddRecFN.scala 600:15]
  wire [10:0] mulAddRecFN_postMul_io_fromPreMul_sExpSum; // @[MulAddRecFN.scala 600:15]
  wire [1:0] mulAddRecFN_postMul_io_fromPreMul_roundingMode; // @[MulAddRecFN.scala 600:15]
  wire [48:0] mulAddRecFN_postMul_io_mulAddResult; // @[MulAddRecFN.scala 600:15]
  wire [32:0] mulAddRecFN_postMul_io_out; // @[MulAddRecFN.scala 600:15]
  wire [4:0] mulAddRecFN_postMul_io_exceptionFlags; // @[MulAddRecFN.scala 600:15]
  wire [47:0] _T_16 = mulAddRecFN_preMul_io_mulAddA * mulAddRecFN_preMul_io_mulAddB; // @[MulAddRecFN.scala 610:39]
  wire [48:0] _T_18 = {1'h0,mulAddRecFN_preMul_io_mulAddC}; // @[Cat.scala 30:58]
  wire [48:0] _GEN_0 = {{1'd0}, _T_16}; // @[MulAddRecFN.scala 610:71]
  MulAddRecFN_preMul mulAddRecFN_preMul ( // @[MulAddRecFN.scala 598:15]
    .io_op(mulAddRecFN_preMul_io_op),
    .io_a(mulAddRecFN_preMul_io_a),
    .io_b(mulAddRecFN_preMul_io_b),
    .io_c(mulAddRecFN_preMul_io_c),
    .io_roundingMode(mulAddRecFN_preMul_io_roundingMode),
    .io_mulAddA(mulAddRecFN_preMul_io_mulAddA),
    .io_mulAddB(mulAddRecFN_preMul_io_mulAddB),
    .io_mulAddC(mulAddRecFN_preMul_io_mulAddC),
    .io_toPostMul_highExpA(mulAddRecFN_preMul_io_toPostMul_highExpA),
    .io_toPostMul_isNaN_isQuietNaNA(mulAddRecFN_preMul_io_toPostMul_isNaN_isQuietNaNA),
    .io_toPostMul_highExpB(mulAddRecFN_preMul_io_toPostMul_highExpB),
    .io_toPostMul_isNaN_isQuietNaNB(mulAddRecFN_preMul_io_toPostMul_isNaN_isQuietNaNB),
    .io_toPostMul_signProd(mulAddRecFN_preMul_io_toPostMul_signProd),
    .io_toPostMul_isZeroProd(mulAddRecFN_preMul_io_toPostMul_isZeroProd),
    .io_toPostMul_opSignC(mulAddRecFN_preMul_io_toPostMul_opSignC),
    .io_toPostMul_highExpC(mulAddRecFN_preMul_io_toPostMul_highExpC),
    .io_toPostMul_isNaN_isQuietNaNC(mulAddRecFN_preMul_io_toPostMul_isNaN_isQuietNaNC),
    .io_toPostMul_isCDominant(mulAddRecFN_preMul_io_toPostMul_isCDominant),
    .io_toPostMul_CAlignDist_0(mulAddRecFN_preMul_io_toPostMul_CAlignDist_0),
    .io_toPostMul_CAlignDist(mulAddRecFN_preMul_io_toPostMul_CAlignDist),
    .io_toPostMul_bit0AlignedNegSigC(mulAddRecFN_preMul_io_toPostMul_bit0AlignedNegSigC),
    .io_toPostMul_highAlignedNegSigC(mulAddRecFN_preMul_io_toPostMul_highAlignedNegSigC),
    .io_toPostMul_sExpSum(mulAddRecFN_preMul_io_toPostMul_sExpSum),
    .io_toPostMul_roundingMode(mulAddRecFN_preMul_io_toPostMul_roundingMode)
  );
  MulAddRecFN_postMul mulAddRecFN_postMul ( // @[MulAddRecFN.scala 600:15]
    .io_fromPreMul_highExpA(mulAddRecFN_postMul_io_fromPreMul_highExpA),
    .io_fromPreMul_isNaN_isQuietNaNA(mulAddRecFN_postMul_io_fromPreMul_isNaN_isQuietNaNA),
    .io_fromPreMul_highExpB(mulAddRecFN_postMul_io_fromPreMul_highExpB),
    .io_fromPreMul_isNaN_isQuietNaNB(mulAddRecFN_postMul_io_fromPreMul_isNaN_isQuietNaNB),
    .io_fromPreMul_signProd(mulAddRecFN_postMul_io_fromPreMul_signProd),
    .io_fromPreMul_isZeroProd(mulAddRecFN_postMul_io_fromPreMul_isZeroProd),
    .io_fromPreMul_opSignC(mulAddRecFN_postMul_io_fromPreMul_opSignC),
    .io_fromPreMul_highExpC(mulAddRecFN_postMul_io_fromPreMul_highExpC),
    .io_fromPreMul_isNaN_isQuietNaNC(mulAddRecFN_postMul_io_fromPreMul_isNaN_isQuietNaNC),
    .io_fromPreMul_isCDominant(mulAddRecFN_postMul_io_fromPreMul_isCDominant),
    .io_fromPreMul_CAlignDist_0(mulAddRecFN_postMul_io_fromPreMul_CAlignDist_0),
    .io_fromPreMul_CAlignDist(mulAddRecFN_postMul_io_fromPreMul_CAlignDist),
    .io_fromPreMul_bit0AlignedNegSigC(mulAddRecFN_postMul_io_fromPreMul_bit0AlignedNegSigC),
    .io_fromPreMul_highAlignedNegSigC(mulAddRecFN_postMul_io_fromPreMul_highAlignedNegSigC),
    .io_fromPreMul_sExpSum(mulAddRecFN_postMul_io_fromPreMul_sExpSum),
    .io_fromPreMul_roundingMode(mulAddRecFN_postMul_io_fromPreMul_roundingMode),
    .io_mulAddResult(mulAddRecFN_postMul_io_mulAddResult),
    .io_out(mulAddRecFN_postMul_io_out),
    .io_exceptionFlags(mulAddRecFN_postMul_io_exceptionFlags)
  );
  assign io_out = mulAddRecFN_postMul_io_out; // @[MulAddRecFN.scala 613:12]
  assign io_exceptionFlags = mulAddRecFN_postMul_io_exceptionFlags; // @[MulAddRecFN.scala 614:23]
  assign mulAddRecFN_preMul_io_op = io_op; // @[MulAddRecFN.scala 602:30]
  assign mulAddRecFN_preMul_io_a = io_a; // @[MulAddRecFN.scala 603:30]
  assign mulAddRecFN_preMul_io_b = io_b; // @[MulAddRecFN.scala 604:30]
  assign mulAddRecFN_preMul_io_c = io_c; // @[MulAddRecFN.scala 605:30]
  assign mulAddRecFN_preMul_io_roundingMode = io_roundingMode; // @[MulAddRecFN.scala 606:40]
  assign mulAddRecFN_postMul_io_fromPreMul_highExpA = mulAddRecFN_preMul_io_toPostMul_highExpA; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_isNaN_isQuietNaNA = mulAddRecFN_preMul_io_toPostMul_isNaN_isQuietNaNA; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_highExpB = mulAddRecFN_preMul_io_toPostMul_highExpB; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_isNaN_isQuietNaNB = mulAddRecFN_preMul_io_toPostMul_isNaN_isQuietNaNB; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_signProd = mulAddRecFN_preMul_io_toPostMul_signProd; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_isZeroProd = mulAddRecFN_preMul_io_toPostMul_isZeroProd; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_opSignC = mulAddRecFN_preMul_io_toPostMul_opSignC; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_highExpC = mulAddRecFN_preMul_io_toPostMul_highExpC; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_isNaN_isQuietNaNC = mulAddRecFN_preMul_io_toPostMul_isNaN_isQuietNaNC; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_isCDominant = mulAddRecFN_preMul_io_toPostMul_isCDominant; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_CAlignDist_0 = mulAddRecFN_preMul_io_toPostMul_CAlignDist_0; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_CAlignDist = mulAddRecFN_preMul_io_toPostMul_CAlignDist; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_bit0AlignedNegSigC = mulAddRecFN_preMul_io_toPostMul_bit0AlignedNegSigC; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_highAlignedNegSigC = mulAddRecFN_preMul_io_toPostMul_highAlignedNegSigC; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_sExpSum = mulAddRecFN_preMul_io_toPostMul_sExpSum; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_roundingMode = mulAddRecFN_preMul_io_toPostMul_roundingMode; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_mulAddResult = _GEN_0 + _T_18; // @[MulAddRecFN.scala 610:71]
endmodule
module CompareRecFN(
  input  [64:0] io_a,
  input  [64:0] io_b,
  input         io_signaling,
  output        io_lt,
  output        io_eq,
  output [4:0]  io_exceptionFlags
);
  wire  rawA_isZero = io_a[63:61] == 3'h0; // @[rawFNFromRecFN.scala 51:54]
  wire  _T_22 = io_a[63:62] == 2'h3; // @[rawFNFromRecFN.scala 52:54]
  wire  rawA_sign = io_a[64]; // @[rawFNFromRecFN.scala 55:23]
  wire  rawA_isNaN = _T_22 & io_a[61]; // @[rawFNFromRecFN.scala 56:32]
  wire  rawA_isInf = _T_22 & ~io_a[61]; // @[rawFNFromRecFN.scala 57:32]
  wire [12:0] rawA_sExp = {1'b0,$signed(io_a[63:52])}; // @[rawFNFromRecFN.scala 59:25]
  wire  _T_46 = ~rawA_isZero; // @[rawFNFromRecFN.scala 60:36]
  wire [55:0] rawA_sig = {1'h0,_T_46,io_a[51:0],2'h0}; // @[Cat.scala 30:58]
  wire  rawB_isZero = io_b[63:61] == 3'h0; // @[rawFNFromRecFN.scala 51:54]
  wire  _T_58 = io_b[63:62] == 2'h3; // @[rawFNFromRecFN.scala 52:54]
  wire  rawB_sign = io_b[64]; // @[rawFNFromRecFN.scala 55:23]
  wire  rawB_isNaN = _T_58 & io_b[61]; // @[rawFNFromRecFN.scala 56:32]
  wire  rawB_isInf = _T_58 & ~io_b[61]; // @[rawFNFromRecFN.scala 57:32]
  wire [12:0] rawB_sExp = {1'b0,$signed(io_b[63:52])}; // @[rawFNFromRecFN.scala 59:25]
  wire  _T_82 = ~rawB_isZero; // @[rawFNFromRecFN.scala 60:36]
  wire [55:0] rawB_sig = {1'h0,_T_82,io_b[51:0],2'h0}; // @[Cat.scala 30:58]
  wire  ordered = ~rawA_isNaN & ~rawB_isNaN; // @[CompareRecFN.scala 57:32]
  wire  bothInfs = rawA_isInf & rawB_isInf; // @[CompareRecFN.scala 58:33]
  wire  bothZeros = rawA_isZero & rawB_isZero; // @[CompareRecFN.scala 59:33]
  wire  eqExps = $signed(rawA_sExp) == $signed(rawB_sExp); // @[CompareRecFN.scala 60:29]
  wire  common_ltMags = $signed(rawA_sExp) < $signed(rawB_sExp) | eqExps & rawA_sig < rawB_sig; // @[CompareRecFN.scala 62:33]
  wire  common_eqMags = eqExps & rawA_sig == rawB_sig; // @[CompareRecFN.scala 63:32]
  wire  _T_99 = ~rawB_sign; // @[CompareRecFN.scala 67:28]
  wire  _T_111 = _T_99 & common_ltMags; // @[CompareRecFN.scala 70:41]
  wire  _T_112 = rawA_sign & ~common_ltMags & ~common_eqMags | _T_111; // @[CompareRecFN.scala 69:74]
  wire  _T_113 = ~bothInfs & _T_112; // @[CompareRecFN.scala 68:30]
  wire  _T_114 = rawA_sign & ~rawB_sign | _T_113; // @[CompareRecFN.scala 67:41]
  wire  ordered_lt = ~bothZeros & _T_114; // @[CompareRecFN.scala 66:21]
  wire  ordered_eq = bothZeros | rawA_sign == rawB_sign & (bothInfs | common_eqMags); // @[CompareRecFN.scala 72:19]
  wire  _T_121 = rawA_isNaN & ~rawA_sig[53]; // @[RoundRawFNToRecFN.scala 61:46]
  wire  _T_125 = rawB_isNaN & ~rawB_sig[53]; // @[RoundRawFNToRecFN.scala 61:46]
  wire  _T_129 = io_signaling & ~ordered; // @[CompareRecFN.scala 76:27]
  wire  invalid = _T_121 | _T_125 | _T_129; // @[CompareRecFN.scala 75:52]
  assign io_lt = ordered & ordered_lt; // @[CompareRecFN.scala 78:22]
  assign io_eq = ordered & ordered_eq; // @[CompareRecFN.scala 79:22]
  assign io_exceptionFlags = {invalid,4'h0}; // @[Cat.scala 30:58]
endmodule
module RecFNToIN(
  input  [64:0] io_in,
  input  [1:0]  io_roundingMode,
  input         io_signedOut,
  output [31:0] io_out,
  output [2:0]  io_intExceptionFlags
);
  wire  sign = io_in[64]; // @[RecFNToIN.scala 54:21]
  wire [11:0] exp = io_in[63:52]; // @[RecFNToIN.scala 55:20]
  wire [51:0] fract = io_in[51:0]; // @[RecFNToIN.scala 56:22]
  wire  isZero = exp[11:9] == 3'h0; // @[RecFNToIN.scala 58:47]
  wire  invalid = exp[11:10] == 2'h3; // @[RecFNToIN.scala 59:50]
  wire  isNaN = invalid & exp[9]; // @[RecFNToIN.scala 60:27]
  wire  notSpecial_magGeOne = exp[11]; // @[RecFNToIN.scala 61:34]
  wire [52:0] _T_17 = {notSpecial_magGeOne,fract}; // @[Cat.scala 30:58]
  wire [4:0] _T_20 = notSpecial_magGeOne ? exp[4:0] : 5'h0; // @[RecFNToIN.scala 73:16]
  wire [83:0] _GEN_0 = {{31'd0}, _T_17}; // @[RecFNToIN.scala 72:40]
  wire [83:0] shiftedSig = _GEN_0 << _T_20; // @[RecFNToIN.scala 72:40]
  wire [31:0] unroundedInt = shiftedSig[83:52]; // @[RecFNToIN.scala 82:24]
  wire  _T_24 = shiftedSig[50:0] != 51'h0; // @[RecFNToIN.scala 86:41]
  wire [2:0] roundBits = {shiftedSig[52:51],_T_24}; // @[Cat.scala 30:58]
  wire  _T_27 = roundBits[1:0] != 2'h0; // @[RecFNToIN.scala 88:65]
  wire  roundInexact = notSpecial_magGeOne ? roundBits[1:0] != 2'h0 : ~isZero; // @[RecFNToIN.scala 88:27]
  wire [1:0] _T_31 = ~roundBits[2:1]; // @[RecFNToIN.scala 91:29]
  wire [1:0] _T_35 = ~roundBits[1:0]; // @[RecFNToIN.scala 91:53]
  wire  _T_38 = _T_31 == 2'h0 | _T_35 == 2'h0; // @[RecFNToIN.scala 91:34]
  wire [10:0] _T_40 = ~exp[10:0]; // @[RecFNToIN.scala 92:38]
  wire  _T_47 = _T_40 == 11'h0 & _T_27; // @[RecFNToIN.scala 92:16]
  wire  roundIncr_nearestEven = notSpecial_magGeOne ? _T_38 : _T_47; // @[RecFNToIN.scala 90:12]
  wire  _T_52 = io_roundingMode == 2'h2 & (sign & roundInexact); // @[RecFNToIN.scala 96:49]
  wire  _T_53 = io_roundingMode == 2'h0 & roundIncr_nearestEven | _T_52; // @[RecFNToIN.scala 95:78]
  wire  _T_56 = ~sign; // @[RecFNToIN.scala 97:53]
  wire  _T_58 = io_roundingMode == 2'h3 & (~sign & roundInexact); // @[RecFNToIN.scala 97:49]
  wire  roundIncr = _T_53 | _T_58; // @[RecFNToIN.scala 96:78]
  wire [31:0] _T_59 = ~unroundedInt; // @[RecFNToIN.scala 98:39]
  wire [31:0] complUnroundedInt = sign ? _T_59 : unroundedInt; // @[RecFNToIN.scala 98:32]
  wire [31:0] _T_63 = complUnroundedInt + 32'h1; // @[RecFNToIN.scala 100:49]
  wire [31:0] roundedInt = roundIncr ^ sign ? _T_63 : complUnroundedInt; // @[RecFNToIN.scala 100:12]
  wire [29:0] _T_65 = ~unroundedInt[29:0]; // @[RecFNToIN.scala 103:56]
  wire  roundCarryBut2 = _T_65 == 30'h0 & roundIncr; // @[RecFNToIN.scala 103:61]
  wire  _T_69 = exp[10:0] >= 11'h20; // @[RecFNToIN.scala 108:21]
  wire  _T_71 = exp[10:0] == 11'h1f; // @[RecFNToIN.scala 109:26]
  wire  _T_77 = _T_56 | unroundedInt[30:0] != 31'h0; // @[RecFNToIN.scala 110:30]
  wire  _T_78 = _T_77 | roundIncr; // @[RecFNToIN.scala 111:27]
  wire  _T_79 = exp[10:0] == 11'h1f & _T_78; // @[RecFNToIN.scala 109:50]
  wire  _T_80 = exp[10:0] >= 11'h20 | _T_79; // @[RecFNToIN.scala 108:40]
  wire  _T_86 = _T_56 & exp[10:0] == 11'h1e & roundCarryBut2; // @[RecFNToIN.scala 112:60]
  wire  _T_87 = _T_80 | _T_86; // @[RecFNToIN.scala 111:42]
  wire  overflow_signed = notSpecial_magGeOne & _T_87; // @[RecFNToIN.scala 107:12]
  wire  _T_95 = _T_71 & unroundedInt[30]; // @[RecFNToIN.scala 118:50]
  wire  _T_96 = _T_95 & roundCarryBut2; // @[RecFNToIN.scala 119:49]
  wire  _T_97 = sign | _T_69 | _T_96; // @[RecFNToIN.scala 117:48]
  wire  _T_98 = sign & roundIncr; // @[RecFNToIN.scala 120:18]
  wire  overflow_unsigned = notSpecial_magGeOne ? _T_97 : _T_98; // @[RecFNToIN.scala 116:12]
  wire  overflow = io_signedOut ? overflow_signed : overflow_unsigned; // @[RecFNToIN.scala 122:23]
  wire  excSign = sign & ~isNaN; // @[RecFNToIN.scala 124:24]
  wire [31:0] _T_105 = io_signedOut & excSign ? 32'h80000000 : 32'h0; // @[RecFNToIN.scala 126:12]
  wire  _T_107 = ~excSign; // @[RecFNToIN.scala 127:29]
  wire [30:0] _T_111 = io_signedOut & ~excSign ? 31'h7fffffff : 31'h0; // @[RecFNToIN.scala 127:12]
  wire [31:0] _GEN_1 = {{1'd0}, _T_111}; // @[RecFNToIN.scala 126:72]
  wire [31:0] _T_112 = _T_105 | _GEN_1; // @[RecFNToIN.scala 126:72]
  wire [31:0] _T_120 = ~io_signedOut & _T_107 ? 32'hffffffff : 32'h0; // @[RecFNToIN.scala 131:12]
  wire [31:0] excValue = _T_112 | _T_120; // @[RecFNToIN.scala 130:11]
  wire  inexact = roundInexact & ~invalid & ~overflow; // @[RecFNToIN.scala 135:45]
  wire [1:0] _T_128 = {invalid,overflow}; // @[Cat.scala 30:58]
  assign io_out = invalid | overflow ? excValue : roundedInt; // @[RecFNToIN.scala 137:18]
  assign io_intExceptionFlags = {_T_128,inexact}; // @[Cat.scala 30:58]
endmodule
module RecFNToIN_1(
  input  [64:0] io_in,
  input  [1:0]  io_roundingMode,
  input         io_signedOut,
  output [63:0] io_out,
  output [2:0]  io_intExceptionFlags
);
  wire  sign = io_in[64]; // @[RecFNToIN.scala 54:21]
  wire [11:0] exp = io_in[63:52]; // @[RecFNToIN.scala 55:20]
  wire [51:0] fract = io_in[51:0]; // @[RecFNToIN.scala 56:22]
  wire  isZero = exp[11:9] == 3'h0; // @[RecFNToIN.scala 58:47]
  wire  invalid = exp[11:10] == 2'h3; // @[RecFNToIN.scala 59:50]
  wire  isNaN = invalid & exp[9]; // @[RecFNToIN.scala 60:27]
  wire  notSpecial_magGeOne = exp[11]; // @[RecFNToIN.scala 61:34]
  wire [52:0] _T_17 = {notSpecial_magGeOne,fract}; // @[Cat.scala 30:58]
  wire [5:0] _T_20 = notSpecial_magGeOne ? exp[5:0] : 6'h0; // @[RecFNToIN.scala 73:16]
  wire [115:0] _GEN_0 = {{63'd0}, _T_17}; // @[RecFNToIN.scala 72:40]
  wire [115:0] shiftedSig = _GEN_0 << _T_20; // @[RecFNToIN.scala 72:40]
  wire [63:0] unroundedInt = shiftedSig[115:52]; // @[RecFNToIN.scala 82:24]
  wire  _T_24 = shiftedSig[50:0] != 51'h0; // @[RecFNToIN.scala 86:41]
  wire [2:0] roundBits = {shiftedSig[52:51],_T_24}; // @[Cat.scala 30:58]
  wire  _T_27 = roundBits[1:0] != 2'h0; // @[RecFNToIN.scala 88:65]
  wire  roundInexact = notSpecial_magGeOne ? roundBits[1:0] != 2'h0 : ~isZero; // @[RecFNToIN.scala 88:27]
  wire [1:0] _T_31 = ~roundBits[2:1]; // @[RecFNToIN.scala 91:29]
  wire [1:0] _T_35 = ~roundBits[1:0]; // @[RecFNToIN.scala 91:53]
  wire  _T_38 = _T_31 == 2'h0 | _T_35 == 2'h0; // @[RecFNToIN.scala 91:34]
  wire [10:0] _T_40 = ~exp[10:0]; // @[RecFNToIN.scala 92:38]
  wire  _T_47 = _T_40 == 11'h0 & _T_27; // @[RecFNToIN.scala 92:16]
  wire  roundIncr_nearestEven = notSpecial_magGeOne ? _T_38 : _T_47; // @[RecFNToIN.scala 90:12]
  wire  _T_52 = io_roundingMode == 2'h2 & (sign & roundInexact); // @[RecFNToIN.scala 96:49]
  wire  _T_53 = io_roundingMode == 2'h0 & roundIncr_nearestEven | _T_52; // @[RecFNToIN.scala 95:78]
  wire  _T_56 = ~sign; // @[RecFNToIN.scala 97:53]
  wire  _T_58 = io_roundingMode == 2'h3 & (~sign & roundInexact); // @[RecFNToIN.scala 97:49]
  wire  roundIncr = _T_53 | _T_58; // @[RecFNToIN.scala 96:78]
  wire [63:0] _T_59 = ~unroundedInt; // @[RecFNToIN.scala 98:39]
  wire [63:0] complUnroundedInt = sign ? _T_59 : unroundedInt; // @[RecFNToIN.scala 98:32]
  wire [63:0] _T_63 = complUnroundedInt + 64'h1; // @[RecFNToIN.scala 100:49]
  wire [63:0] roundedInt = roundIncr ^ sign ? _T_63 : complUnroundedInt; // @[RecFNToIN.scala 100:12]
  wire [61:0] _T_65 = ~unroundedInt[61:0]; // @[RecFNToIN.scala 103:56]
  wire  roundCarryBut2 = _T_65 == 62'h0 & roundIncr; // @[RecFNToIN.scala 103:61]
  wire  _T_69 = exp[10:0] >= 11'h40; // @[RecFNToIN.scala 108:21]
  wire  _T_71 = exp[10:0] == 11'h3f; // @[RecFNToIN.scala 109:26]
  wire  _T_77 = _T_56 | unroundedInt[62:0] != 63'h0; // @[RecFNToIN.scala 110:30]
  wire  _T_78 = _T_77 | roundIncr; // @[RecFNToIN.scala 111:27]
  wire  _T_79 = exp[10:0] == 11'h3f & _T_78; // @[RecFNToIN.scala 109:50]
  wire  _T_80 = exp[10:0] >= 11'h40 | _T_79; // @[RecFNToIN.scala 108:40]
  wire  _T_86 = _T_56 & exp[10:0] == 11'h3e & roundCarryBut2; // @[RecFNToIN.scala 112:60]
  wire  _T_87 = _T_80 | _T_86; // @[RecFNToIN.scala 111:42]
  wire  overflow_signed = notSpecial_magGeOne & _T_87; // @[RecFNToIN.scala 107:12]
  wire  _T_95 = _T_71 & unroundedInt[62]; // @[RecFNToIN.scala 118:50]
  wire  _T_96 = _T_95 & roundCarryBut2; // @[RecFNToIN.scala 119:49]
  wire  _T_97 = sign | _T_69 | _T_96; // @[RecFNToIN.scala 117:48]
  wire  _T_98 = sign & roundIncr; // @[RecFNToIN.scala 120:18]
  wire  overflow_unsigned = notSpecial_magGeOne ? _T_97 : _T_98; // @[RecFNToIN.scala 116:12]
  wire  overflow = io_signedOut ? overflow_signed : overflow_unsigned; // @[RecFNToIN.scala 122:23]
  wire  excSign = sign & ~isNaN; // @[RecFNToIN.scala 124:24]
  wire [63:0] _T_105 = io_signedOut & excSign ? 64'h8000000000000000 : 64'h0; // @[RecFNToIN.scala 126:12]
  wire  _T_107 = ~excSign; // @[RecFNToIN.scala 127:29]
  wire [62:0] _T_111 = io_signedOut & ~excSign ? 63'h7fffffffffffffff : 63'h0; // @[RecFNToIN.scala 127:12]
  wire [63:0] _GEN_1 = {{1'd0}, _T_111}; // @[RecFNToIN.scala 126:72]
  wire [63:0] _T_112 = _T_105 | _GEN_1; // @[RecFNToIN.scala 126:72]
  wire [63:0] _T_120 = ~io_signedOut & _T_107 ? 64'hffffffffffffffff : 64'h0; // @[RecFNToIN.scala 131:12]
  wire [63:0] excValue = _T_112 | _T_120; // @[RecFNToIN.scala 130:11]
  wire  inexact = roundInexact & ~invalid & ~overflow; // @[RecFNToIN.scala 135:45]
  wire [1:0] _T_128 = {invalid,overflow}; // @[Cat.scala 30:58]
  assign io_out = invalid | overflow ? excValue : roundedInt; // @[RecFNToIN.scala 137:18]
  assign io_intExceptionFlags = {_T_128,inexact}; // @[Cat.scala 30:58]
endmodule
module INToRecFN(
  input         io_signedIn,
  input  [63:0] io_in,
  input  [1:0]  io_roundingMode,
  output [32:0] io_out,
  output [4:0]  io_exceptionFlags
);
  wire  sign = io_signedIn & io_in[63]; // @[INToRecFN.scala 55:28]
  wire [63:0] _T_16 = 64'h0 - io_in; // @[INToRecFN.scala 56:27]
  wire [63:0] absIn = sign ? _T_16 : io_in; // @[INToRecFN.scala 56:20]
  wire  _T_21 = absIn[63:32] != 32'h0; // @[CircuitMath.scala 37:22]
  wire  _T_25 = absIn[63:48] != 16'h0; // @[CircuitMath.scala 37:22]
  wire  _T_29 = absIn[63:56] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_33 = absIn[63:60] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_39 = absIn[62] ? 2'h2 : {{1'd0}, absIn[61]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_40 = absIn[63] ? 2'h3 : _T_39; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_46 = absIn[58] ? 2'h2 : {{1'd0}, absIn[57]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_47 = absIn[59] ? 2'h3 : _T_46; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_48 = _T_33 ? _T_40 : _T_47; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_49 = {_T_33,_T_48}; // @[Cat.scala 30:58]
  wire  _T_53 = absIn[55:52] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_59 = absIn[54] ? 2'h2 : {{1'd0}, absIn[53]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_60 = absIn[55] ? 2'h3 : _T_59; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_66 = absIn[50] ? 2'h2 : {{1'd0}, absIn[49]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_67 = absIn[51] ? 2'h3 : _T_66; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_68 = _T_53 ? _T_60 : _T_67; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_69 = {_T_53,_T_68}; // @[Cat.scala 30:58]
  wire [2:0] _T_70 = _T_29 ? _T_49 : _T_69; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_71 = {_T_29,_T_70}; // @[Cat.scala 30:58]
  wire  _T_75 = absIn[47:40] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_79 = absIn[47:44] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_85 = absIn[46] ? 2'h2 : {{1'd0}, absIn[45]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_86 = absIn[47] ? 2'h3 : _T_85; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_92 = absIn[42] ? 2'h2 : {{1'd0}, absIn[41]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_93 = absIn[43] ? 2'h3 : _T_92; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_94 = _T_79 ? _T_86 : _T_93; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_95 = {_T_79,_T_94}; // @[Cat.scala 30:58]
  wire  _T_99 = absIn[39:36] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_105 = absIn[38] ? 2'h2 : {{1'd0}, absIn[37]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_106 = absIn[39] ? 2'h3 : _T_105; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_112 = absIn[34] ? 2'h2 : {{1'd0}, absIn[33]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_113 = absIn[35] ? 2'h3 : _T_112; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_114 = _T_99 ? _T_106 : _T_113; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_115 = {_T_99,_T_114}; // @[Cat.scala 30:58]
  wire [2:0] _T_116 = _T_75 ? _T_95 : _T_115; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_117 = {_T_75,_T_116}; // @[Cat.scala 30:58]
  wire [3:0] _T_118 = _T_25 ? _T_71 : _T_117; // @[CircuitMath.scala 38:21]
  wire [4:0] _T_119 = {_T_25,_T_118}; // @[Cat.scala 30:58]
  wire  _T_123 = absIn[31:16] != 16'h0; // @[CircuitMath.scala 37:22]
  wire  _T_127 = absIn[31:24] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_131 = absIn[31:28] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_137 = absIn[30] ? 2'h2 : {{1'd0}, absIn[29]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_138 = absIn[31] ? 2'h3 : _T_137; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_144 = absIn[26] ? 2'h2 : {{1'd0}, absIn[25]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_145 = absIn[27] ? 2'h3 : _T_144; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_146 = _T_131 ? _T_138 : _T_145; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_147 = {_T_131,_T_146}; // @[Cat.scala 30:58]
  wire  _T_151 = absIn[23:20] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_157 = absIn[22] ? 2'h2 : {{1'd0}, absIn[21]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_158 = absIn[23] ? 2'h3 : _T_157; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_164 = absIn[18] ? 2'h2 : {{1'd0}, absIn[17]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_165 = absIn[19] ? 2'h3 : _T_164; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_166 = _T_151 ? _T_158 : _T_165; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_167 = {_T_151,_T_166}; // @[Cat.scala 30:58]
  wire [2:0] _T_168 = _T_127 ? _T_147 : _T_167; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_169 = {_T_127,_T_168}; // @[Cat.scala 30:58]
  wire  _T_173 = absIn[15:8] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_177 = absIn[15:12] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_183 = absIn[14] ? 2'h2 : {{1'd0}, absIn[13]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_184 = absIn[15] ? 2'h3 : _T_183; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_190 = absIn[10] ? 2'h2 : {{1'd0}, absIn[9]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_191 = absIn[11] ? 2'h3 : _T_190; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_192 = _T_177 ? _T_184 : _T_191; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_193 = {_T_177,_T_192}; // @[Cat.scala 30:58]
  wire  _T_197 = absIn[7:4] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_203 = absIn[6] ? 2'h2 : {{1'd0}, absIn[5]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_204 = absIn[7] ? 2'h3 : _T_203; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_210 = absIn[2] ? 2'h2 : {{1'd0}, absIn[1]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_211 = absIn[3] ? 2'h3 : _T_210; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_212 = _T_197 ? _T_204 : _T_211; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_213 = {_T_197,_T_212}; // @[Cat.scala 30:58]
  wire [2:0] _T_214 = _T_173 ? _T_193 : _T_213; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_215 = {_T_173,_T_214}; // @[Cat.scala 30:58]
  wire [3:0] _T_216 = _T_123 ? _T_169 : _T_215; // @[CircuitMath.scala 38:21]
  wire [4:0] _T_217 = {_T_123,_T_216}; // @[Cat.scala 30:58]
  wire [4:0] _T_218 = _T_21 ? _T_119 : _T_217; // @[CircuitMath.scala 38:21]
  wire [5:0] _T_219 = {_T_21,_T_218}; // @[Cat.scala 30:58]
  wire [5:0] normCount = ~_T_219; // @[INToRecFN.scala 57:21]
  wire [126:0] _GEN_0 = {{63'd0}, absIn}; // @[INToRecFN.scala 58:27]
  wire [126:0] _T_220 = _GEN_0 << normCount; // @[INToRecFN.scala 58:27]
  wire [63:0] normAbsIn = _T_220[63:0]; // @[INToRecFN.scala 58:39]
  wire  _T_225 = normAbsIn[38:0] != 39'h0; // @[INToRecFN.scala 64:55]
  wire [2:0] roundBits = {normAbsIn[40:39],_T_225}; // @[Cat.scala 30:58]
  wire  roundInexact = roundBits[1:0] != 2'h0; // @[INToRecFN.scala 72:40]
  wire [1:0] _T_230 = ~roundBits[2:1]; // @[INToRecFN.scala 75:29]
  wire [1:0] _T_234 = ~roundBits[1:0]; // @[INToRecFN.scala 75:53]
  wire  _T_237 = _T_230 == 2'h0 | _T_234 == 2'h0; // @[INToRecFN.scala 75:34]
  wire  _T_239 = io_roundingMode == 2'h0 & _T_237; // @[INToRecFN.scala 74:12]
  wire  _T_241 = sign & roundInexact; // @[INToRecFN.scala 79:18]
  wire  _T_243 = io_roundingMode == 2'h2 & _T_241; // @[INToRecFN.scala 78:12]
  wire  _T_244 = _T_239 | _T_243; // @[INToRecFN.scala 77:11]
  wire  _T_248 = ~sign & roundInexact; // @[INToRecFN.scala 83:20]
  wire  _T_250 = io_roundingMode == 2'h3 & _T_248; // @[INToRecFN.scala 82:12]
  wire  round = _T_244 | _T_250; // @[INToRecFN.scala 81:11]
  wire [24:0] unroundedNorm = {1'h0,normAbsIn[63:40]}; // @[Cat.scala 30:58]
  wire [24:0] _T_256 = unroundedNorm + 25'h1; // @[INToRecFN.scala 94:48]
  wire [24:0] roundedNorm = round ? _T_256 : unroundedNorm; // @[INToRecFN.scala 94:26]
  wire [5:0] _T_257 = ~normCount; // @[INToRecFN.scala 97:24]
  wire [7:0] _T_260 = {1'h0,1'h0,_T_257}; // @[Cat.scala 30:58]
  wire [7:0] _GEN_1 = {{7'd0}, roundedNorm[24]}; // @[INToRecFN.scala 106:52]
  wire [7:0] roundedExp = _T_260 + _GEN_1; // @[INToRecFN.scala 106:52]
  wire [9:0] _T_268 = {sign,normAbsIn[63],roundedExp}; // @[Cat.scala 30:58]
  wire [1:0] _T_272 = {1'h0,roundInexact}; // @[Cat.scala 30:58]
  assign io_out = {_T_268,roundedNorm[22:0]}; // @[Cat.scala 30:58]
  assign io_exceptionFlags = {3'h0,_T_272}; // @[Cat.scala 30:58]
endmodule
module INToRecFN_1(
  input         io_signedIn,
  input  [63:0] io_in,
  input  [1:0]  io_roundingMode,
  output [64:0] io_out,
  output [4:0]  io_exceptionFlags
);
  wire  sign = io_signedIn & io_in[63]; // @[INToRecFN.scala 55:28]
  wire [63:0] _T_16 = 64'h0 - io_in; // @[INToRecFN.scala 56:27]
  wire [63:0] absIn = sign ? _T_16 : io_in; // @[INToRecFN.scala 56:20]
  wire  _T_21 = absIn[63:32] != 32'h0; // @[CircuitMath.scala 37:22]
  wire  _T_25 = absIn[63:48] != 16'h0; // @[CircuitMath.scala 37:22]
  wire  _T_29 = absIn[63:56] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_33 = absIn[63:60] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_39 = absIn[62] ? 2'h2 : {{1'd0}, absIn[61]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_40 = absIn[63] ? 2'h3 : _T_39; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_46 = absIn[58] ? 2'h2 : {{1'd0}, absIn[57]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_47 = absIn[59] ? 2'h3 : _T_46; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_48 = _T_33 ? _T_40 : _T_47; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_49 = {_T_33,_T_48}; // @[Cat.scala 30:58]
  wire  _T_53 = absIn[55:52] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_59 = absIn[54] ? 2'h2 : {{1'd0}, absIn[53]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_60 = absIn[55] ? 2'h3 : _T_59; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_66 = absIn[50] ? 2'h2 : {{1'd0}, absIn[49]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_67 = absIn[51] ? 2'h3 : _T_66; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_68 = _T_53 ? _T_60 : _T_67; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_69 = {_T_53,_T_68}; // @[Cat.scala 30:58]
  wire [2:0] _T_70 = _T_29 ? _T_49 : _T_69; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_71 = {_T_29,_T_70}; // @[Cat.scala 30:58]
  wire  _T_75 = absIn[47:40] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_79 = absIn[47:44] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_85 = absIn[46] ? 2'h2 : {{1'd0}, absIn[45]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_86 = absIn[47] ? 2'h3 : _T_85; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_92 = absIn[42] ? 2'h2 : {{1'd0}, absIn[41]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_93 = absIn[43] ? 2'h3 : _T_92; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_94 = _T_79 ? _T_86 : _T_93; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_95 = {_T_79,_T_94}; // @[Cat.scala 30:58]
  wire  _T_99 = absIn[39:36] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_105 = absIn[38] ? 2'h2 : {{1'd0}, absIn[37]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_106 = absIn[39] ? 2'h3 : _T_105; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_112 = absIn[34] ? 2'h2 : {{1'd0}, absIn[33]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_113 = absIn[35] ? 2'h3 : _T_112; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_114 = _T_99 ? _T_106 : _T_113; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_115 = {_T_99,_T_114}; // @[Cat.scala 30:58]
  wire [2:0] _T_116 = _T_75 ? _T_95 : _T_115; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_117 = {_T_75,_T_116}; // @[Cat.scala 30:58]
  wire [3:0] _T_118 = _T_25 ? _T_71 : _T_117; // @[CircuitMath.scala 38:21]
  wire [4:0] _T_119 = {_T_25,_T_118}; // @[Cat.scala 30:58]
  wire  _T_123 = absIn[31:16] != 16'h0; // @[CircuitMath.scala 37:22]
  wire  _T_127 = absIn[31:24] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_131 = absIn[31:28] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_137 = absIn[30] ? 2'h2 : {{1'd0}, absIn[29]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_138 = absIn[31] ? 2'h3 : _T_137; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_144 = absIn[26] ? 2'h2 : {{1'd0}, absIn[25]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_145 = absIn[27] ? 2'h3 : _T_144; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_146 = _T_131 ? _T_138 : _T_145; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_147 = {_T_131,_T_146}; // @[Cat.scala 30:58]
  wire  _T_151 = absIn[23:20] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_157 = absIn[22] ? 2'h2 : {{1'd0}, absIn[21]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_158 = absIn[23] ? 2'h3 : _T_157; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_164 = absIn[18] ? 2'h2 : {{1'd0}, absIn[17]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_165 = absIn[19] ? 2'h3 : _T_164; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_166 = _T_151 ? _T_158 : _T_165; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_167 = {_T_151,_T_166}; // @[Cat.scala 30:58]
  wire [2:0] _T_168 = _T_127 ? _T_147 : _T_167; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_169 = {_T_127,_T_168}; // @[Cat.scala 30:58]
  wire  _T_173 = absIn[15:8] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_177 = absIn[15:12] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_183 = absIn[14] ? 2'h2 : {{1'd0}, absIn[13]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_184 = absIn[15] ? 2'h3 : _T_183; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_190 = absIn[10] ? 2'h2 : {{1'd0}, absIn[9]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_191 = absIn[11] ? 2'h3 : _T_190; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_192 = _T_177 ? _T_184 : _T_191; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_193 = {_T_177,_T_192}; // @[Cat.scala 30:58]
  wire  _T_197 = absIn[7:4] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_203 = absIn[6] ? 2'h2 : {{1'd0}, absIn[5]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_204 = absIn[7] ? 2'h3 : _T_203; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_210 = absIn[2] ? 2'h2 : {{1'd0}, absIn[1]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_211 = absIn[3] ? 2'h3 : _T_210; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_212 = _T_197 ? _T_204 : _T_211; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_213 = {_T_197,_T_212}; // @[Cat.scala 30:58]
  wire [2:0] _T_214 = _T_173 ? _T_193 : _T_213; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_215 = {_T_173,_T_214}; // @[Cat.scala 30:58]
  wire [3:0] _T_216 = _T_123 ? _T_169 : _T_215; // @[CircuitMath.scala 38:21]
  wire [4:0] _T_217 = {_T_123,_T_216}; // @[Cat.scala 30:58]
  wire [4:0] _T_218 = _T_21 ? _T_119 : _T_217; // @[CircuitMath.scala 38:21]
  wire [5:0] _T_219 = {_T_21,_T_218}; // @[Cat.scala 30:58]
  wire [5:0] normCount = ~_T_219; // @[INToRecFN.scala 57:21]
  wire [126:0] _GEN_0 = {{63'd0}, absIn}; // @[INToRecFN.scala 58:27]
  wire [126:0] _T_220 = _GEN_0 << normCount; // @[INToRecFN.scala 58:27]
  wire [63:0] normAbsIn = _T_220[63:0]; // @[INToRecFN.scala 58:39]
  wire  _T_225 = normAbsIn[9:0] != 10'h0; // @[INToRecFN.scala 64:55]
  wire [2:0] roundBits = {normAbsIn[11:10],_T_225}; // @[Cat.scala 30:58]
  wire  roundInexact = roundBits[1:0] != 2'h0; // @[INToRecFN.scala 72:40]
  wire [1:0] _T_230 = ~roundBits[2:1]; // @[INToRecFN.scala 75:29]
  wire [1:0] _T_234 = ~roundBits[1:0]; // @[INToRecFN.scala 75:53]
  wire  _T_237 = _T_230 == 2'h0 | _T_234 == 2'h0; // @[INToRecFN.scala 75:34]
  wire  _T_239 = io_roundingMode == 2'h0 & _T_237; // @[INToRecFN.scala 74:12]
  wire  _T_241 = sign & roundInexact; // @[INToRecFN.scala 79:18]
  wire  _T_243 = io_roundingMode == 2'h2 & _T_241; // @[INToRecFN.scala 78:12]
  wire  _T_244 = _T_239 | _T_243; // @[INToRecFN.scala 77:11]
  wire  _T_248 = ~sign & roundInexact; // @[INToRecFN.scala 83:20]
  wire  _T_250 = io_roundingMode == 2'h3 & _T_248; // @[INToRecFN.scala 82:12]
  wire  round = _T_244 | _T_250; // @[INToRecFN.scala 81:11]
  wire [53:0] unroundedNorm = {1'h0,normAbsIn[63:11]}; // @[Cat.scala 30:58]
  wire [53:0] _T_256 = unroundedNorm + 54'h1; // @[INToRecFN.scala 94:48]
  wire [53:0] roundedNorm = round ? _T_256 : unroundedNorm; // @[INToRecFN.scala 94:26]
  wire [5:0] _T_257 = ~normCount; // @[INToRecFN.scala 97:24]
  wire [10:0] _T_260 = {1'h0,4'h0,_T_257}; // @[Cat.scala 30:58]
  wire [10:0] _GEN_1 = {{10'd0}, roundedNorm[53]}; // @[INToRecFN.scala 106:52]
  wire [10:0] roundedExp = _T_260 + _GEN_1; // @[INToRecFN.scala 106:52]
  wire [12:0] _T_268 = {sign,normAbsIn[63],roundedExp}; // @[Cat.scala 30:58]
  wire [1:0] _T_272 = {1'h0,roundInexact}; // @[Cat.scala 30:58]
  assign io_out = {_T_268,roundedNorm[51:0]}; // @[Cat.scala 30:58]
  assign io_exceptionFlags = {3'h0,_T_272}; // @[Cat.scala 30:58]
endmodule
module RecFNToRecFN_1(
  input  [32:0] io_in,
  output [64:0] io_out,
  output [4:0]  io_exceptionFlags
);
  wire  outRawFloat_isZero = io_in[31:29] == 3'h0; // @[rawFNFromRecFN.scala 51:54]
  wire  _T_16 = io_in[31:30] == 2'h3; // @[rawFNFromRecFN.scala 52:54]
  wire  outRawFloat_sign = io_in[32]; // @[rawFNFromRecFN.scala 55:23]
  wire  outRawFloat_isNaN = _T_16 & io_in[29]; // @[rawFNFromRecFN.scala 56:32]
  wire  outRawFloat_isInf = _T_16 & ~io_in[29]; // @[rawFNFromRecFN.scala 57:32]
  wire [9:0] _T_38 = {1'b0,$signed(io_in[31:23])}; // @[rawFNFromRecFN.scala 59:25]
  wire  _T_41 = ~outRawFloat_isZero; // @[rawFNFromRecFN.scala 60:36]
  wire [26:0] _T_46 = {1'h0,_T_41,io_in[22:0],2'h0}; // @[Cat.scala 30:58]
  wire [11:0] _GEN_0 = {{2{_T_38[9]}},_T_38}; // @[resizeRawFN.scala 49:31]
  wire [12:0] outRawFloat_sExp = $signed(_GEN_0) + 12'sh700; // @[resizeRawFN.scala 49:31]
  wire [55:0] outRawFloat_sig = {_T_46, 29'h0}; // @[resizeRawFN.scala 69:24]
  wire  invalidExc = outRawFloat_isNaN & ~outRawFloat_sig[53]; // @[RoundRawFNToRecFN.scala 61:46]
  wire  _T_68 = outRawFloat_sign & ~outRawFloat_isNaN; // @[RecFNToRecFN.scala 69:37]
  wire [11:0] _T_72 = outRawFloat_isZero ? 12'hc00 : 12'h0; // @[RecFNToRecFN.scala 72:22]
  wire [11:0] _T_73 = ~_T_72; // @[RecFNToRecFN.scala 72:18]
  wire [11:0] _T_74 = outRawFloat_sExp[11:0] & _T_73; // @[RecFNToRecFN.scala 71:47]
  wire [11:0] _T_78 = outRawFloat_isZero | outRawFloat_isInf ? 12'h200 : 12'h0; // @[RecFNToRecFN.scala 76:22]
  wire [11:0] _T_79 = ~_T_78; // @[RecFNToRecFN.scala 76:18]
  wire [11:0] _T_80 = _T_74 & _T_79; // @[RecFNToRecFN.scala 75:21]
  wire [11:0] _T_83 = outRawFloat_isInf ? 12'hc00 : 12'h0; // @[RecFNToRecFN.scala 80:20]
  wire [11:0] _T_84 = _T_80 | _T_83; // @[RecFNToRecFN.scala 79:22]
  wire [11:0] _T_87 = outRawFloat_isNaN ? 12'he00 : 12'h0; // @[RecFNToRecFN.scala 84:20]
  wire [11:0] _T_88 = _T_84 | _T_87; // @[RecFNToRecFN.scala 83:19]
  wire [51:0] _T_92 = outRawFloat_isNaN ? 52'h8000000000000 : outRawFloat_sig[53:2]; // @[RecFNToRecFN.scala 89:16]
  wire [12:0] _T_93 = {_T_68,_T_88}; // @[Cat.scala 30:58]
  assign io_out = {_T_93,_T_92}; // @[Cat.scala 30:58]
  assign io_exceptionFlags = {invalidExc,4'h0}; // @[Cat.scala 30:58]
endmodule
module MulAddRecFN_1(
  input  [1:0]  io_op,
  input  [64:0] io_a,
  input  [64:0] io_b,
  input  [64:0] io_c,
  input  [1:0]  io_roundingMode,
  output [64:0] io_out,
  output [4:0]  io_exceptionFlags
);
  wire [1:0] mulAddRecFN_preMul_io_op; // @[MulAddRecFN.scala 598:15]
  wire [64:0] mulAddRecFN_preMul_io_a; // @[MulAddRecFN.scala 598:15]
  wire [64:0] mulAddRecFN_preMul_io_b; // @[MulAddRecFN.scala 598:15]
  wire [64:0] mulAddRecFN_preMul_io_c; // @[MulAddRecFN.scala 598:15]
  wire [1:0] mulAddRecFN_preMul_io_roundingMode; // @[MulAddRecFN.scala 598:15]
  wire [52:0] mulAddRecFN_preMul_io_mulAddA; // @[MulAddRecFN.scala 598:15]
  wire [52:0] mulAddRecFN_preMul_io_mulAddB; // @[MulAddRecFN.scala 598:15]
  wire [105:0] mulAddRecFN_preMul_io_mulAddC; // @[MulAddRecFN.scala 598:15]
  wire [2:0] mulAddRecFN_preMul_io_toPostMul_highExpA; // @[MulAddRecFN.scala 598:15]
  wire  mulAddRecFN_preMul_io_toPostMul_isNaN_isQuietNaNA; // @[MulAddRecFN.scala 598:15]
  wire [2:0] mulAddRecFN_preMul_io_toPostMul_highExpB; // @[MulAddRecFN.scala 598:15]
  wire  mulAddRecFN_preMul_io_toPostMul_isNaN_isQuietNaNB; // @[MulAddRecFN.scala 598:15]
  wire  mulAddRecFN_preMul_io_toPostMul_signProd; // @[MulAddRecFN.scala 598:15]
  wire  mulAddRecFN_preMul_io_toPostMul_isZeroProd; // @[MulAddRecFN.scala 598:15]
  wire  mulAddRecFN_preMul_io_toPostMul_opSignC; // @[MulAddRecFN.scala 598:15]
  wire [2:0] mulAddRecFN_preMul_io_toPostMul_highExpC; // @[MulAddRecFN.scala 598:15]
  wire  mulAddRecFN_preMul_io_toPostMul_isNaN_isQuietNaNC; // @[MulAddRecFN.scala 598:15]
  wire  mulAddRecFN_preMul_io_toPostMul_isCDominant; // @[MulAddRecFN.scala 598:15]
  wire  mulAddRecFN_preMul_io_toPostMul_CAlignDist_0; // @[MulAddRecFN.scala 598:15]
  wire [7:0] mulAddRecFN_preMul_io_toPostMul_CAlignDist; // @[MulAddRecFN.scala 598:15]
  wire  mulAddRecFN_preMul_io_toPostMul_bit0AlignedNegSigC; // @[MulAddRecFN.scala 598:15]
  wire [54:0] mulAddRecFN_preMul_io_toPostMul_highAlignedNegSigC; // @[MulAddRecFN.scala 598:15]
  wire [13:0] mulAddRecFN_preMul_io_toPostMul_sExpSum; // @[MulAddRecFN.scala 598:15]
  wire [1:0] mulAddRecFN_preMul_io_toPostMul_roundingMode; // @[MulAddRecFN.scala 598:15]
  wire [2:0] mulAddRecFN_postMul_io_fromPreMul_highExpA; // @[MulAddRecFN.scala 600:15]
  wire  mulAddRecFN_postMul_io_fromPreMul_isNaN_isQuietNaNA; // @[MulAddRecFN.scala 600:15]
  wire [2:0] mulAddRecFN_postMul_io_fromPreMul_highExpB; // @[MulAddRecFN.scala 600:15]
  wire  mulAddRecFN_postMul_io_fromPreMul_isNaN_isQuietNaNB; // @[MulAddRecFN.scala 600:15]
  wire  mulAddRecFN_postMul_io_fromPreMul_signProd; // @[MulAddRecFN.scala 600:15]
  wire  mulAddRecFN_postMul_io_fromPreMul_isZeroProd; // @[MulAddRecFN.scala 600:15]
  wire  mulAddRecFN_postMul_io_fromPreMul_opSignC; // @[MulAddRecFN.scala 600:15]
  wire [2:0] mulAddRecFN_postMul_io_fromPreMul_highExpC; // @[MulAddRecFN.scala 600:15]
  wire  mulAddRecFN_postMul_io_fromPreMul_isNaN_isQuietNaNC; // @[MulAddRecFN.scala 600:15]
  wire  mulAddRecFN_postMul_io_fromPreMul_isCDominant; // @[MulAddRecFN.scala 600:15]
  wire  mulAddRecFN_postMul_io_fromPreMul_CAlignDist_0; // @[MulAddRecFN.scala 600:15]
  wire [7:0] mulAddRecFN_postMul_io_fromPreMul_CAlignDist; // @[MulAddRecFN.scala 600:15]
  wire  mulAddRecFN_postMul_io_fromPreMul_bit0AlignedNegSigC; // @[MulAddRecFN.scala 600:15]
  wire [54:0] mulAddRecFN_postMul_io_fromPreMul_highAlignedNegSigC; // @[MulAddRecFN.scala 600:15]
  wire [13:0] mulAddRecFN_postMul_io_fromPreMul_sExpSum; // @[MulAddRecFN.scala 600:15]
  wire [1:0] mulAddRecFN_postMul_io_fromPreMul_roundingMode; // @[MulAddRecFN.scala 600:15]
  wire [106:0] mulAddRecFN_postMul_io_mulAddResult; // @[MulAddRecFN.scala 600:15]
  wire [64:0] mulAddRecFN_postMul_io_out; // @[MulAddRecFN.scala 600:15]
  wire [4:0] mulAddRecFN_postMul_io_exceptionFlags; // @[MulAddRecFN.scala 600:15]
  wire [105:0] _T_16 = mulAddRecFN_preMul_io_mulAddA * mulAddRecFN_preMul_io_mulAddB; // @[MulAddRecFN.scala 610:39]
  wire [106:0] _T_18 = {1'h0,mulAddRecFN_preMul_io_mulAddC}; // @[Cat.scala 30:58]
  wire [106:0] _GEN_0 = {{1'd0}, _T_16}; // @[MulAddRecFN.scala 610:71]
  MulAddRecFN_preMul_1 mulAddRecFN_preMul ( // @[MulAddRecFN.scala 598:15]
    .io_op(mulAddRecFN_preMul_io_op),
    .io_a(mulAddRecFN_preMul_io_a),
    .io_b(mulAddRecFN_preMul_io_b),
    .io_c(mulAddRecFN_preMul_io_c),
    .io_roundingMode(mulAddRecFN_preMul_io_roundingMode),
    .io_mulAddA(mulAddRecFN_preMul_io_mulAddA),
    .io_mulAddB(mulAddRecFN_preMul_io_mulAddB),
    .io_mulAddC(mulAddRecFN_preMul_io_mulAddC),
    .io_toPostMul_highExpA(mulAddRecFN_preMul_io_toPostMul_highExpA),
    .io_toPostMul_isNaN_isQuietNaNA(mulAddRecFN_preMul_io_toPostMul_isNaN_isQuietNaNA),
    .io_toPostMul_highExpB(mulAddRecFN_preMul_io_toPostMul_highExpB),
    .io_toPostMul_isNaN_isQuietNaNB(mulAddRecFN_preMul_io_toPostMul_isNaN_isQuietNaNB),
    .io_toPostMul_signProd(mulAddRecFN_preMul_io_toPostMul_signProd),
    .io_toPostMul_isZeroProd(mulAddRecFN_preMul_io_toPostMul_isZeroProd),
    .io_toPostMul_opSignC(mulAddRecFN_preMul_io_toPostMul_opSignC),
    .io_toPostMul_highExpC(mulAddRecFN_preMul_io_toPostMul_highExpC),
    .io_toPostMul_isNaN_isQuietNaNC(mulAddRecFN_preMul_io_toPostMul_isNaN_isQuietNaNC),
    .io_toPostMul_isCDominant(mulAddRecFN_preMul_io_toPostMul_isCDominant),
    .io_toPostMul_CAlignDist_0(mulAddRecFN_preMul_io_toPostMul_CAlignDist_0),
    .io_toPostMul_CAlignDist(mulAddRecFN_preMul_io_toPostMul_CAlignDist),
    .io_toPostMul_bit0AlignedNegSigC(mulAddRecFN_preMul_io_toPostMul_bit0AlignedNegSigC),
    .io_toPostMul_highAlignedNegSigC(mulAddRecFN_preMul_io_toPostMul_highAlignedNegSigC),
    .io_toPostMul_sExpSum(mulAddRecFN_preMul_io_toPostMul_sExpSum),
    .io_toPostMul_roundingMode(mulAddRecFN_preMul_io_toPostMul_roundingMode)
  );
  MulAddRecFN_postMul_1 mulAddRecFN_postMul ( // @[MulAddRecFN.scala 600:15]
    .io_fromPreMul_highExpA(mulAddRecFN_postMul_io_fromPreMul_highExpA),
    .io_fromPreMul_isNaN_isQuietNaNA(mulAddRecFN_postMul_io_fromPreMul_isNaN_isQuietNaNA),
    .io_fromPreMul_highExpB(mulAddRecFN_postMul_io_fromPreMul_highExpB),
    .io_fromPreMul_isNaN_isQuietNaNB(mulAddRecFN_postMul_io_fromPreMul_isNaN_isQuietNaNB),
    .io_fromPreMul_signProd(mulAddRecFN_postMul_io_fromPreMul_signProd),
    .io_fromPreMul_isZeroProd(mulAddRecFN_postMul_io_fromPreMul_isZeroProd),
    .io_fromPreMul_opSignC(mulAddRecFN_postMul_io_fromPreMul_opSignC),
    .io_fromPreMul_highExpC(mulAddRecFN_postMul_io_fromPreMul_highExpC),
    .io_fromPreMul_isNaN_isQuietNaNC(mulAddRecFN_postMul_io_fromPreMul_isNaN_isQuietNaNC),
    .io_fromPreMul_isCDominant(mulAddRecFN_postMul_io_fromPreMul_isCDominant),
    .io_fromPreMul_CAlignDist_0(mulAddRecFN_postMul_io_fromPreMul_CAlignDist_0),
    .io_fromPreMul_CAlignDist(mulAddRecFN_postMul_io_fromPreMul_CAlignDist),
    .io_fromPreMul_bit0AlignedNegSigC(mulAddRecFN_postMul_io_fromPreMul_bit0AlignedNegSigC),
    .io_fromPreMul_highAlignedNegSigC(mulAddRecFN_postMul_io_fromPreMul_highAlignedNegSigC),
    .io_fromPreMul_sExpSum(mulAddRecFN_postMul_io_fromPreMul_sExpSum),
    .io_fromPreMul_roundingMode(mulAddRecFN_postMul_io_fromPreMul_roundingMode),
    .io_mulAddResult(mulAddRecFN_postMul_io_mulAddResult),
    .io_out(mulAddRecFN_postMul_io_out),
    .io_exceptionFlags(mulAddRecFN_postMul_io_exceptionFlags)
  );
  assign io_out = mulAddRecFN_postMul_io_out; // @[MulAddRecFN.scala 613:12]
  assign io_exceptionFlags = mulAddRecFN_postMul_io_exceptionFlags; // @[MulAddRecFN.scala 614:23]
  assign mulAddRecFN_preMul_io_op = io_op; // @[MulAddRecFN.scala 602:30]
  assign mulAddRecFN_preMul_io_a = io_a; // @[MulAddRecFN.scala 603:30]
  assign mulAddRecFN_preMul_io_b = io_b; // @[MulAddRecFN.scala 604:30]
  assign mulAddRecFN_preMul_io_c = io_c; // @[MulAddRecFN.scala 605:30]
  assign mulAddRecFN_preMul_io_roundingMode = io_roundingMode; // @[MulAddRecFN.scala 606:40]
  assign mulAddRecFN_postMul_io_fromPreMul_highExpA = mulAddRecFN_preMul_io_toPostMul_highExpA; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_isNaN_isQuietNaNA = mulAddRecFN_preMul_io_toPostMul_isNaN_isQuietNaNA; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_highExpB = mulAddRecFN_preMul_io_toPostMul_highExpB; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_isNaN_isQuietNaNB = mulAddRecFN_preMul_io_toPostMul_isNaN_isQuietNaNB; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_signProd = mulAddRecFN_preMul_io_toPostMul_signProd; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_isZeroProd = mulAddRecFN_preMul_io_toPostMul_isZeroProd; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_opSignC = mulAddRecFN_preMul_io_toPostMul_opSignC; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_highExpC = mulAddRecFN_preMul_io_toPostMul_highExpC; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_isNaN_isQuietNaNC = mulAddRecFN_preMul_io_toPostMul_isNaN_isQuietNaNC; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_isCDominant = mulAddRecFN_preMul_io_toPostMul_isCDominant; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_CAlignDist_0 = mulAddRecFN_preMul_io_toPostMul_CAlignDist_0; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_CAlignDist = mulAddRecFN_preMul_io_toPostMul_CAlignDist; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_bit0AlignedNegSigC = mulAddRecFN_preMul_io_toPostMul_bit0AlignedNegSigC; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_highAlignedNegSigC = mulAddRecFN_preMul_io_toPostMul_highAlignedNegSigC; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_sExpSum = mulAddRecFN_preMul_io_toPostMul_sExpSum; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_fromPreMul_roundingMode = mulAddRecFN_preMul_io_toPostMul_roundingMode; // @[MulAddRecFN.scala 608:39]
  assign mulAddRecFN_postMul_io_mulAddResult = _GEN_0 + _T_18; // @[MulAddRecFN.scala 610:71]
endmodule
module DivSqrtRecF64_mulAddZ31(
  input          clock,
  input          reset,
  output         io_inReady_div,
  output         io_inReady_sqrt,
  input          io_inValid,
  input          io_sqrtOp,
  input  [64:0]  io_a,
  input  [64:0]  io_b,
  input  [1:0]   io_roundingMode,
  output         io_outValid_div,
  output         io_outValid_sqrt,
  output [64:0]  io_out,
  output [4:0]   io_exceptionFlags,
  output [3:0]   io_usingMulAdd,
  output         io_latchMulAddA_0,
  output [53:0]  io_mulAddA_0,
  output         io_latchMulAddB_0,
  output [53:0]  io_mulAddB_0,
  output [104:0] io_mulAddC_2,
  input  [104:0] io_mulAddResult_3
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_3;
  reg [31:0] _RAND_4;
  reg [31:0] _RAND_5;
  reg [31:0] _RAND_6;
  reg [31:0] _RAND_7;
  reg [31:0] _RAND_8;
  reg [63:0] _RAND_9;
  reg [63:0] _RAND_10;
  reg [31:0] _RAND_11;
  reg [31:0] _RAND_12;
  reg [31:0] _RAND_13;
  reg [31:0] _RAND_14;
  reg [31:0] _RAND_15;
  reg [31:0] _RAND_16;
  reg [31:0] _RAND_17;
  reg [31:0] _RAND_18;
  reg [31:0] _RAND_19;
  reg [31:0] _RAND_20;
  reg [63:0] _RAND_21;
  reg [31:0] _RAND_22;
  reg [31:0] _RAND_23;
  reg [31:0] _RAND_24;
  reg [31:0] _RAND_25;
  reg [31:0] _RAND_26;
  reg [31:0] _RAND_27;
  reg [31:0] _RAND_28;
  reg [31:0] _RAND_29;
  reg [31:0] _RAND_30;
  reg [31:0] _RAND_31;
  reg [63:0] _RAND_32;
  reg [31:0] _RAND_33;
  reg [31:0] _RAND_34;
  reg [31:0] _RAND_35;
  reg [31:0] _RAND_36;
  reg [31:0] _RAND_37;
  reg [31:0] _RAND_38;
  reg [31:0] _RAND_39;
  reg [31:0] _RAND_40;
  reg [31:0] _RAND_41;
  reg [31:0] _RAND_42;
  reg [31:0] _RAND_43;
  reg [63:0] _RAND_44;
  reg [63:0] _RAND_45;
  reg [63:0] _RAND_46;
  reg [31:0] _RAND_47;
  reg [31:0] _RAND_48;
  reg [63:0] _RAND_49;
  reg [31:0] _RAND_50;
  reg [31:0] _RAND_51;
  reg [31:0] _RAND_52;
`endif // RANDOMIZE_REG_INIT
  reg  valid_PA; // @[DivSqrtRecF64_mulAddZ31.scala 78:30]
  reg  sqrtOp_PA; // @[DivSqrtRecF64_mulAddZ31.scala 79:30]
  reg  sign_PA; // @[DivSqrtRecF64_mulAddZ31.scala 80:30]
  reg [2:0] specialCodeB_PA; // @[DivSqrtRecF64_mulAddZ31.scala 82:30]
  reg  fractB_51_PA; // @[DivSqrtRecF64_mulAddZ31.scala 83:30]
  reg [1:0] roundingMode_PA; // @[DivSqrtRecF64_mulAddZ31.scala 84:30]
  reg [2:0] specialCodeA_PA; // @[DivSqrtRecF64_mulAddZ31.scala 85:30]
  reg  fractA_51_PA; // @[DivSqrtRecF64_mulAddZ31.scala 86:30]
  reg [13:0] exp_PA; // @[DivSqrtRecF64_mulAddZ31.scala 87:30]
  reg [50:0] fractB_other_PA; // @[DivSqrtRecF64_mulAddZ31.scala 88:30]
  reg [50:0] fractA_other_PA; // @[DivSqrtRecF64_mulAddZ31.scala 89:30]
  reg  valid_PB; // @[DivSqrtRecF64_mulAddZ31.scala 91:30]
  reg  sqrtOp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 92:30]
  reg  sign_PB; // @[DivSqrtRecF64_mulAddZ31.scala 93:30]
  reg [2:0] specialCodeA_PB; // @[DivSqrtRecF64_mulAddZ31.scala 95:30]
  reg  fractA_51_PB; // @[DivSqrtRecF64_mulAddZ31.scala 96:30]
  reg [2:0] specialCodeB_PB; // @[DivSqrtRecF64_mulAddZ31.scala 97:30]
  reg  fractB_51_PB; // @[DivSqrtRecF64_mulAddZ31.scala 98:30]
  reg [1:0] roundingMode_PB; // @[DivSqrtRecF64_mulAddZ31.scala 99:30]
  reg [13:0] exp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 100:30]
  reg  fractA_0_PB; // @[DivSqrtRecF64_mulAddZ31.scala 101:30]
  reg [50:0] fractB_other_PB; // @[DivSqrtRecF64_mulAddZ31.scala 102:30]
  reg  valid_PC; // @[DivSqrtRecF64_mulAddZ31.scala 104:30]
  reg  sqrtOp_PC; // @[DivSqrtRecF64_mulAddZ31.scala 105:30]
  reg  sign_PC; // @[DivSqrtRecF64_mulAddZ31.scala 106:30]
  reg [2:0] specialCodeA_PC; // @[DivSqrtRecF64_mulAddZ31.scala 108:30]
  reg  fractA_51_PC; // @[DivSqrtRecF64_mulAddZ31.scala 109:30]
  reg [2:0] specialCodeB_PC; // @[DivSqrtRecF64_mulAddZ31.scala 110:30]
  reg  fractB_51_PC; // @[DivSqrtRecF64_mulAddZ31.scala 111:30]
  reg [1:0] roundingMode_PC; // @[DivSqrtRecF64_mulAddZ31.scala 112:30]
  reg [13:0] exp_PC; // @[DivSqrtRecF64_mulAddZ31.scala 113:30]
  reg  fractA_0_PC; // @[DivSqrtRecF64_mulAddZ31.scala 114:30]
  reg [50:0] fractB_other_PC; // @[DivSqrtRecF64_mulAddZ31.scala 115:30]
  reg [2:0] cycleNum_A; // @[DivSqrtRecF64_mulAddZ31.scala 117:30]
  reg [3:0] cycleNum_B; // @[DivSqrtRecF64_mulAddZ31.scala 118:30]
  reg [2:0] cycleNum_C; // @[DivSqrtRecF64_mulAddZ31.scala 119:30]
  reg [2:0] cycleNum_E; // @[DivSqrtRecF64_mulAddZ31.scala 120:30]
  reg [8:0] fractR0_A; // @[DivSqrtRecF64_mulAddZ31.scala 122:30]
  reg [9:0] hiSqrR0_A_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 124:30]
  reg [20:0] partNegSigma0_A; // @[DivSqrtRecF64_mulAddZ31.scala 125:30]
  reg [8:0] nextMulAdd9A_A; // @[DivSqrtRecF64_mulAddZ31.scala 126:30]
  reg [8:0] nextMulAdd9B_A; // @[DivSqrtRecF64_mulAddZ31.scala 127:30]
  reg [16:0] ER1_B_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 128:30]
  reg [31:0] ESqrR1_B_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 130:30]
  reg [57:0] sigX1_B; // @[DivSqrtRecF64_mulAddZ31.scala 131:30]
  reg [32:0] sqrSigma1_C; // @[DivSqrtRecF64_mulAddZ31.scala 132:30]
  reg [57:0] sigXN_C; // @[DivSqrtRecF64_mulAddZ31.scala 133:30]
  reg [30:0] u_C_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 134:30]
  reg  E_E_div; // @[DivSqrtRecF64_mulAddZ31.scala 135:30]
  reg [52:0] sigT_E; // @[DivSqrtRecF64_mulAddZ31.scala 136:30]
  reg  extraT_E; // @[DivSqrtRecF64_mulAddZ31.scala 137:30]
  reg  isNegRemT_E; // @[DivSqrtRecF64_mulAddZ31.scala 138:30]
  reg  isZeroRemT_E; // @[DivSqrtRecF64_mulAddZ31.scala 139:30]
  wire  cyc_B7_sqrt = cycleNum_B == 4'h7; // @[DivSqrtRecF64_mulAddZ31.scala 426:33]
  wire  _T_277 = ~valid_PA; // @[DivSqrtRecF64_mulAddZ31.scala 284:17]
  wire  isSpecialB_PA = specialCodeB_PA[2:1] == 2'h3; // @[DivSqrtRecF64_mulAddZ31.scala 271:48]
  wire  _T_257 = ~isSpecialB_PA; // @[DivSqrtRecF64_mulAddZ31.scala 276:13]
  wire  isZeroB_PA = specialCodeB_PA == 3'h0; // @[DivSqrtRecF64_mulAddZ31.scala 270:42]
  wire  _T_259 = ~isZeroB_PA; // @[DivSqrtRecF64_mulAddZ31.scala 276:32]
  wire  _T_263 = ~isSpecialB_PA & ~isZeroB_PA & ~sign_PA; // @[DivSqrtRecF64_mulAddZ31.scala 276:45]
  wire  isSpecialA_PA = specialCodeA_PA[2:1] == 2'h3; // @[DivSqrtRecF64_mulAddZ31.scala 267:48]
  wire  isZeroA_PA = specialCodeA_PA == 3'h0; // @[DivSqrtRecF64_mulAddZ31.scala 266:42]
  wire  _T_274 = ~isSpecialA_PA & _T_257 & ~isZeroA_PA & _T_259; // @[DivSqrtRecF64_mulAddZ31.scala 277:64]
  wire  normalCase_PA = sqrtOp_PA ? _T_263 : _T_274; // @[DivSqrtRecF64_mulAddZ31.scala 275:12]
  wire  cyc_B4 = cycleNum_B == 4'h4; // @[DivSqrtRecF64_mulAddZ31.scala 430:27]
  wire  _T_465 = ~sqrtOp_PA; // @[DivSqrtRecF64_mulAddZ31.scala 437:41]
  wire  cyc_B4_div = cyc_B4 & valid_PA & ~sqrtOp_PA; // @[DivSqrtRecF64_mulAddZ31.scala 437:38]
  wire  valid_normalCase_leaving_PA = cyc_B4_div | cyc_B7_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 280:50]
  wire  _T_318 = ~valid_PB; // @[DivSqrtRecF64_mulAddZ31.scala 322:17]
  wire  isSpecialB_PB = specialCodeB_PB[2:1] == 2'h3; // @[DivSqrtRecF64_mulAddZ31.scala 311:48]
  wire  _T_298 = ~isSpecialB_PB; // @[DivSqrtRecF64_mulAddZ31.scala 314:13]
  wire  isZeroB_PB = specialCodeB_PB == 3'h0; // @[DivSqrtRecF64_mulAddZ31.scala 310:42]
  wire  _T_300 = ~isZeroB_PB; // @[DivSqrtRecF64_mulAddZ31.scala 314:32]
  wire  _T_304 = ~isSpecialB_PB & ~isZeroB_PB & ~sign_PB; // @[DivSqrtRecF64_mulAddZ31.scala 314:45]
  wire  isSpecialA_PB = specialCodeA_PB[2:1] == 2'h3; // @[DivSqrtRecF64_mulAddZ31.scala 309:48]
  wire  isZeroA_PB = specialCodeA_PB == 3'h0; // @[DivSqrtRecF64_mulAddZ31.scala 308:42]
  wire  _T_315 = ~isSpecialA_PB & _T_298 & ~isZeroA_PB & _T_300; // @[DivSqrtRecF64_mulAddZ31.scala 315:64]
  wire  normalCase_PB = sqrtOp_PB ? _T_304 : _T_315; // @[DivSqrtRecF64_mulAddZ31.scala 313:12]
  wire  cyc_C3 = cycleNum_C == 3'h3; // @[DivSqrtRecF64_mulAddZ31.scala 458:27]
  wire  isSpecialB_PC = specialCodeB_PC[2:1] == 2'h3; // @[DivSqrtRecF64_mulAddZ31.scala 353:48]
  wire  _T_352 = ~isSpecialB_PC; // @[DivSqrtRecF64_mulAddZ31.scala 360:24]
  wire  isZeroB_PC = specialCodeB_PC == 3'h0; // @[DivSqrtRecF64_mulAddZ31.scala 352:42]
  wire  _T_354 = ~isZeroB_PC; // @[DivSqrtRecF64_mulAddZ31.scala 360:43]
  wire  isSpecialA_PC = specialCodeA_PC[2:1] == 2'h3; // @[DivSqrtRecF64_mulAddZ31.scala 347:48]
  wire  _T_360 = ~isSpecialA_PC; // @[DivSqrtRecF64_mulAddZ31.scala 361:13]
  wire  isZeroA_PC = specialCodeA_PC == 3'h0; // @[DivSqrtRecF64_mulAddZ31.scala 346:42]
  wire  _T_365 = ~isZeroA_PC; // @[DivSqrtRecF64_mulAddZ31.scala 361:51]
  wire  _T_369 = ~isSpecialA_PC & _T_352 & ~isZeroA_PC & _T_354; // @[DivSqrtRecF64_mulAddZ31.scala 361:64]
  wire  normalCase_PC = sqrtOp_PC ? ~isSpecialB_PC & ~isZeroB_PC & ~sign_PC : _T_369; // @[DivSqrtRecF64_mulAddZ31.scala 360:12]
  wire  cyc_E1 = cycleNum_E == 3'h1; // @[DivSqrtRecF64_mulAddZ31.scala 481:27]
  wire  valid_leaving_PC = ~normalCase_PC | cyc_E1; // @[DivSqrtRecF64_mulAddZ31.scala 380:44]
  wire  ready_PC = ~valid_PC | valid_leaving_PC; // @[DivSqrtRecF64_mulAddZ31.scala 382:28]
  wire  valid_leaving_PB = normalCase_PB ? cyc_C3 : ready_PC; // @[DivSqrtRecF64_mulAddZ31.scala 320:12]
  wire  ready_PB = ~valid_PB | valid_leaving_PB; // @[DivSqrtRecF64_mulAddZ31.scala 322:28]
  wire  valid_leaving_PA = normalCase_PA ? valid_normalCase_leaving_PA : ready_PB; // @[DivSqrtRecF64_mulAddZ31.scala 282:12]
  wire  ready_PA = ~valid_PA | valid_leaving_PA; // @[DivSqrtRecF64_mulAddZ31.scala 284:28]
  wire  cyc_B6 = cycleNum_B == 4'h6; // @[DivSqrtRecF64_mulAddZ31.scala 428:27]
  wire  cyc_B6_sqrt = cyc_B6 & valid_PB & sqrtOp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 442:39]
  wire  _T_136 = ~cyc_B6_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 197:38]
  wire  cyc_B5 = cycleNum_B == 4'h5; // @[DivSqrtRecF64_mulAddZ31.scala 429:27]
  wire  cyc_B5_sqrt = cyc_B5 & valid_PB & sqrtOp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 443:39]
  wire  _T_139 = ~cyc_B5_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 197:55]
  wire  cyc_B4_sqrt = cyc_B4 & valid_PB & sqrtOp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 444:39]
  wire  _T_142 = ~cyc_B4_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 198:13]
  wire  _T_143 = ready_PA & ~cyc_B7_sqrt & ~cyc_B6_sqrt & ~cyc_B5_sqrt & _T_142; // @[DivSqrtRecF64_mulAddZ31.scala 197:69]
  wire  cyc_B3 = cycleNum_B == 4'h3; // @[DivSqrtRecF64_mulAddZ31.scala 431:27]
  wire  cyc_B2 = cycleNum_B == 4'h2; // @[DivSqrtRecF64_mulAddZ31.scala 432:27]
  wire  cyc_B1 = cycleNum_B == 4'h1; // @[DivSqrtRecF64_mulAddZ31.scala 433:27]
  wire  cyc_B1_sqrt = cyc_B1 & sqrtOp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 447:27]
  wire  _T_151 = ~cyc_B1_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 198:54]
  wire  cyc_C5 = cycleNum_C == 3'h5; // @[DivSqrtRecF64_mulAddZ31.scala 456:27]
  wire  _T_154 = ~cyc_C5; // @[DivSqrtRecF64_mulAddZ31.scala 199:13]
  wire  _T_155 = _T_143 & ~cyc_B3 & ~cyc_B2 & ~cyc_B1_sqrt & _T_154; // @[DivSqrtRecF64_mulAddZ31.scala 198:68]
  wire  cyc_C4 = cycleNum_C == 3'h4; // @[DivSqrtRecF64_mulAddZ31.scala 457:27]
  wire  _T_471 = ~sqrtOp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 439:29]
  wire  cyc_B2_div = cyc_B2 & ~sqrtOp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 439:26]
  wire  _T_169 = ~cyc_B2_div; // @[DivSqrtRecF64_mulAddZ31.scala 202:13]
  wire  _T_170 = ready_PA & _T_136 & _T_139 & _T_142 & _T_169; // @[DivSqrtRecF64_mulAddZ31.scala 201:69]
  wire  _T_176 = ~io_sqrtOp; // @[DivSqrtRecF64_mulAddZ31.scala 203:55]
  wire  cyc_S_div = io_inReady_div & io_inValid & ~io_sqrtOp; // @[DivSqrtRecF64_mulAddZ31.scala 203:52]
  wire  cyc_S_sqrt = io_inReady_sqrt & io_inValid & io_sqrtOp; // @[DivSqrtRecF64_mulAddZ31.scala 204:52]
  wire  cyc_S = cyc_S_div | cyc_S_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 205:27]
  wire  signA_S = io_a[64]; // @[DivSqrtRecF64_mulAddZ31.scala 207:24]
  wire [11:0] expA_S = io_a[63:52]; // @[DivSqrtRecF64_mulAddZ31.scala 208:24]
  wire [51:0] fractA_S = io_a[51:0]; // @[DivSqrtRecF64_mulAddZ31.scala 209:24]
  wire [2:0] specialCodeA_S = expA_S[11:9]; // @[DivSqrtRecF64_mulAddZ31.scala 210:32]
  wire  isZeroA_S = specialCodeA_S == 3'h0; // @[DivSqrtRecF64_mulAddZ31.scala 211:40]
  wire  isSpecialA_S = specialCodeA_S[2:1] == 2'h3; // @[DivSqrtRecF64_mulAddZ31.scala 212:46]
  wire  signB_S = io_b[64]; // @[DivSqrtRecF64_mulAddZ31.scala 214:24]
  wire [11:0] expB_S = io_b[63:52]; // @[DivSqrtRecF64_mulAddZ31.scala 215:24]
  wire [51:0] fractB_S = io_b[51:0]; // @[DivSqrtRecF64_mulAddZ31.scala 216:24]
  wire [2:0] specialCodeB_S = expB_S[11:9]; // @[DivSqrtRecF64_mulAddZ31.scala 217:32]
  wire  isZeroB_S = specialCodeB_S == 3'h0; // @[DivSqrtRecF64_mulAddZ31.scala 218:40]
  wire  isSpecialB_S = specialCodeB_S[2:1] == 2'h3; // @[DivSqrtRecF64_mulAddZ31.scala 219:46]
  wire  _T_188 = ~isSpecialB_S; // @[DivSqrtRecF64_mulAddZ31.scala 224:27]
  wire  _T_194 = ~isZeroB_S; // @[DivSqrtRecF64_mulAddZ31.scala 224:60]
  wire  normalCase_S_div = ~isSpecialA_S & ~isSpecialB_S & ~isZeroA_S & ~isZeroB_S; // @[DivSqrtRecF64_mulAddZ31.scala 224:57]
  wire  normalCase_S_sqrt = _T_188 & _T_194 & ~signB_S; // @[DivSqrtRecF64_mulAddZ31.scala 225:59]
  wire  normalCase_S = io_sqrtOp ? normalCase_S_sqrt : normalCase_S_div; // @[DivSqrtRecF64_mulAddZ31.scala 226:27]
  wire  cyc_A4_div = cyc_S_div & normalCase_S_div; // @[DivSqrtRecF64_mulAddZ31.scala 228:50]
  wire  cyc_A7_sqrt = cyc_S_sqrt & normalCase_S_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 229:50]
  wire  entering_PA_normalCase = cyc_A4_div | cyc_A7_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 231:36]
  wire  entering_PA = entering_PA_normalCase | cyc_S & (valid_PA | ~ready_PB); // @[DivSqrtRecF64_mulAddZ31.scala 233:32]
  wire  _T_211 = cyc_S & ~normalCase_S & _T_277; // @[DivSqrtRecF64_mulAddZ31.scala 235:33]
  wire  leaving_PB = valid_PB & valid_leaving_PB; // @[DivSqrtRecF64_mulAddZ31.scala 321:28]
  wire  _T_217 = leaving_PB | _T_318 & ~ready_PC; // @[DivSqrtRecF64_mulAddZ31.scala 236:25]
  wire  entering_PB_S = cyc_S & ~normalCase_S & _T_277 & _T_217; // @[DivSqrtRecF64_mulAddZ31.scala 235:47]
  wire  entering_PC_S = _T_211 & _T_318 & ready_PC; // @[DivSqrtRecF64_mulAddZ31.scala 238:61]
  wire  leaving_PA = valid_PA & valid_leaving_PA; // @[DivSqrtRecF64_mulAddZ31.scala 283:28]
  wire [2:0] _T_237 = expB_S[11] ? 3'h7 : 3'h0; // @[Bitwise.scala 71:12]
  wire [10:0] _T_239 = ~expB_S[10:0]; // @[DivSqrtRecF64_mulAddZ31.scala 258:51]
  wire [13:0] _T_240 = {_T_237,_T_239}; // @[Cat.scala 30:58]
  wire [13:0] _GEN_53 = {{2'd0}, expA_S}; // @[DivSqrtRecF64_mulAddZ31.scala 258:24]
  wire [13:0] _T_242 = _GEN_53 + _T_240; // @[DivSqrtRecF64_mulAddZ31.scala 258:24]
  wire [52:0] sigA_PA = {1'h1,fractA_51_PA,fractA_other_PA}; // @[Cat.scala 30:58]
  wire [52:0] sigB_PA = {1'h1,fractB_51_PA,fractB_other_PA}; // @[Cat.scala 30:58]
  wire  entering_PB_normalCase = valid_PA & normalCase_PA & valid_normalCase_leaving_PA; // @[DivSqrtRecF64_mulAddZ31.scala 287:35]
  wire  entering_PB = entering_PB_S | leaving_PA; // @[DivSqrtRecF64_mulAddZ31.scala 288:37]
  wire  entering_PC_normalCase = valid_PB & normalCase_PB & cyc_C3; // @[DivSqrtRecF64_mulAddZ31.scala 325:35]
  wire  entering_PC = entering_PC_S | leaving_PB; // @[DivSqrtRecF64_mulAddZ31.scala 326:37]
  wire  leaving_PC = valid_PC & valid_leaving_PC; // @[DivSqrtRecF64_mulAddZ31.scala 381:28]
  wire  isInfA_PC = isSpecialA_PC & ~specialCodeA_PC[0]; // @[DivSqrtRecF64_mulAddZ31.scala 348:39]
  wire  isNaNA_PC = isSpecialA_PC & specialCodeA_PC[0]; // @[DivSqrtRecF64_mulAddZ31.scala 349:39]
  wire  isSigNaNA_PC = isNaNA_PC & ~fractA_51_PC; // @[DivSqrtRecF64_mulAddZ31.scala 350:35]
  wire  isInfB_PC = isSpecialB_PC & ~specialCodeB_PC[0]; // @[DivSqrtRecF64_mulAddZ31.scala 354:39]
  wire  isNaNB_PC = isSpecialB_PC & specialCodeB_PC[0]; // @[DivSqrtRecF64_mulAddZ31.scala 355:39]
  wire  isSigNaNB_PC = isNaNB_PC & ~fractB_51_PC; // @[DivSqrtRecF64_mulAddZ31.scala 356:35]
  wire [52:0] sigB_PC = {1'h1,fractB_51_PC,fractB_other_PC}; // @[Cat.scala 30:58]
  wire [13:0] expP2_PC = exp_PC + 14'h2; // @[DivSqrtRecF64_mulAddZ31.scala 363:27]
  wire [13:0] _T_375 = {expP2_PC[13:1],1'h0}; // @[Cat.scala 30:58]
  wire [13:0] _T_378 = {exp_PC[13:1],1'h1}; // @[Cat.scala 30:58]
  wire [13:0] expP1_PC = exp_PC[0] ? _T_375 : _T_378; // @[DivSqrtRecF64_mulAddZ31.scala 365:12]
  wire  roundingMode_near_even_PC = roundingMode_PC == 2'h0; // @[DivSqrtRecF64_mulAddZ31.scala 370:54]
  wire  roundingMode_min_PC = roundingMode_PC == 2'h2; // @[DivSqrtRecF64_mulAddZ31.scala 372:54]
  wire  roundingMode_max_PC = roundingMode_PC == 2'h3; // @[DivSqrtRecF64_mulAddZ31.scala 373:54]
  wire  roundMagUp_PC = sign_PC ? roundingMode_min_PC : roundingMode_max_PC; // @[DivSqrtRecF64_mulAddZ31.scala 376:12]
  wire  overflowY_roundMagUp_PC = roundingMode_near_even_PC | roundMagUp_PC; // @[DivSqrtRecF64_mulAddZ31.scala 377:61]
  wire  _T_380 = ~roundMagUp_PC; // @[DivSqrtRecF64_mulAddZ31.scala 378:27]
  wire  roundMagDown_PC = ~roundMagUp_PC & ~roundingMode_near_even_PC; // @[DivSqrtRecF64_mulAddZ31.scala 378:43]
  wire  _T_390 = ~sqrtOp_PC; // @[DivSqrtRecF64_mulAddZ31.scala 383:39]
  wire [1:0] _T_398 = cyc_A4_div ? 2'h3 : 2'h0; // @[DivSqrtRecF64_mulAddZ31.scala 390:16]
  wire [2:0] _T_401 = cyc_A7_sqrt ? 3'h6 : 3'h0; // @[DivSqrtRecF64_mulAddZ31.scala 391:16]
  wire [2:0] _GEN_54 = {{1'd0}, _T_398}; // @[DivSqrtRecF64_mulAddZ31.scala 390:74]
  wire [2:0] _T_402 = _GEN_54 | _T_401; // @[DivSqrtRecF64_mulAddZ31.scala 390:74]
  wire [2:0] _T_408 = cycleNum_A - 3'h1; // @[DivSqrtRecF64_mulAddZ31.scala 392:54]
  wire [2:0] _T_410 = ~entering_PA_normalCase ? _T_408 : 3'h0; // @[DivSqrtRecF64_mulAddZ31.scala 392:16]
  wire [2:0] _T_411 = _T_402 | _T_410; // @[DivSqrtRecF64_mulAddZ31.scala 391:74]
  wire  cyc_A6_sqrt = cycleNum_A == 3'h6; // @[DivSqrtRecF64_mulAddZ31.scala 396:35]
  wire  cyc_A5_sqrt = cycleNum_A == 3'h5; // @[DivSqrtRecF64_mulAddZ31.scala 397:35]
  wire  cyc_A4_sqrt = cycleNum_A == 3'h4; // @[DivSqrtRecF64_mulAddZ31.scala 398:35]
  wire  cyc_A4 = cyc_A4_sqrt | cyc_A4_div; // @[DivSqrtRecF64_mulAddZ31.scala 402:30]
  wire  cyc_A3 = cycleNum_A == 3'h3; // @[DivSqrtRecF64_mulAddZ31.scala 403:30]
  wire  cyc_A2 = cycleNum_A == 3'h2; // @[DivSqrtRecF64_mulAddZ31.scala 404:30]
  wire  cyc_A1 = cycleNum_A == 3'h1; // @[DivSqrtRecF64_mulAddZ31.scala 405:30]
  wire  cyc_A3_div = cyc_A3 & _T_465; // @[DivSqrtRecF64_mulAddZ31.scala 407:29]
  wire  cyc_A2_div = cyc_A2 & _T_465; // @[DivSqrtRecF64_mulAddZ31.scala 408:29]
  wire  cyc_A1_div = cyc_A1 & _T_465; // @[DivSqrtRecF64_mulAddZ31.scala 409:29]
  wire  cyc_A3_sqrt = cyc_A3 & sqrtOp_PA; // @[DivSqrtRecF64_mulAddZ31.scala 411:30]
  wire  cyc_A1_sqrt = cyc_A1 & sqrtOp_PA; // @[DivSqrtRecF64_mulAddZ31.scala 413:30]
  wire [3:0] _T_433 = cycleNum_B - 4'h1; // @[DivSqrtRecF64_mulAddZ31.scala 419:28]
  wire  cyc_B10_sqrt = cycleNum_B == 4'ha; // @[DivSqrtRecF64_mulAddZ31.scala 423:33]
  wire  cyc_B9_sqrt = cycleNum_B == 4'h9; // @[DivSqrtRecF64_mulAddZ31.scala 424:33]
  wire  cyc_B8_sqrt = cycleNum_B == 4'h8; // @[DivSqrtRecF64_mulAddZ31.scala 425:33]
  wire  cyc_B6_div = cyc_B6 & valid_PA & _T_465; // @[DivSqrtRecF64_mulAddZ31.scala 435:38]
  wire  cyc_B1_div = cyc_B1 & _T_471; // @[DivSqrtRecF64_mulAddZ31.scala 440:26]
  wire  cyc_B3_sqrt = cyc_B3 & sqrtOp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 445:27]
  wire  cyc_B2_sqrt = cyc_B2 & sqrtOp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 446:27]
  wire [2:0] _T_494 = cycleNum_C - 3'h1; // @[DivSqrtRecF64_mulAddZ31.scala 451:70]
  wire  cyc_C6_sqrt = cycleNum_C == 3'h6; // @[DivSqrtRecF64_mulAddZ31.scala 454:35]
  wire  cyc_C2 = cycleNum_C == 3'h2; // @[DivSqrtRecF64_mulAddZ31.scala 459:27]
  wire  cyc_C1 = cycleNum_C == 3'h1; // @[DivSqrtRecF64_mulAddZ31.scala 460:27]
  wire  cyc_C5_div = cyc_C5 & _T_471; // @[DivSqrtRecF64_mulAddZ31.scala 462:29]
  wire  cyc_C4_div = cyc_C4 & _T_471; // @[DivSqrtRecF64_mulAddZ31.scala 463:29]
  wire  cyc_C1_div = cyc_C1 & _T_390; // @[DivSqrtRecF64_mulAddZ31.scala 466:29]
  wire  cyc_C5_sqrt = cyc_C5 & sqrtOp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 468:30]
  wire  cyc_C4_sqrt = cyc_C4 & sqrtOp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 469:30]
  wire  cyc_C3_sqrt = cyc_C3 & sqrtOp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 470:30]
  wire  cyc_C1_sqrt = cyc_C1 & sqrtOp_PC; // @[DivSqrtRecF64_mulAddZ31.scala 472:30]
  wire [2:0] _T_524 = cycleNum_E - 3'h1; // @[DivSqrtRecF64_mulAddZ31.scala 475:55]
  wire  cyc_E3 = cycleNum_E == 3'h3; // @[DivSqrtRecF64_mulAddZ31.scala 479:27]
  wire  cyc_E2 = cycleNum_E == 3'h2; // @[DivSqrtRecF64_mulAddZ31.scala 480:27]
  wire  cyc_E3_div = cyc_E3 & _T_390; // @[DivSqrtRecF64_mulAddZ31.scala 484:29]
  wire  cyc_E3_sqrt = cyc_E3 & sqrtOp_PC; // @[DivSqrtRecF64_mulAddZ31.scala 489:30]
  wire [51:0] zFractB_A4_div = cyc_A4_div ? fractB_S : 52'h0; // @[DivSqrtRecF64_mulAddZ31.scala 496:29]
  wire  zLinPiece_0_A4_div = cyc_A4_div & fractB_S[51:49] == 3'h0; // @[DivSqrtRecF64_mulAddZ31.scala 498:41]
  wire  zLinPiece_1_A4_div = cyc_A4_div & fractB_S[51:49] == 3'h1; // @[DivSqrtRecF64_mulAddZ31.scala 499:41]
  wire  zLinPiece_2_A4_div = cyc_A4_div & fractB_S[51:49] == 3'h2; // @[DivSqrtRecF64_mulAddZ31.scala 500:41]
  wire  zLinPiece_3_A4_div = cyc_A4_div & fractB_S[51:49] == 3'h3; // @[DivSqrtRecF64_mulAddZ31.scala 501:41]
  wire  zLinPiece_4_A4_div = cyc_A4_div & fractB_S[51:49] == 3'h4; // @[DivSqrtRecF64_mulAddZ31.scala 502:41]
  wire  zLinPiece_5_A4_div = cyc_A4_div & fractB_S[51:49] == 3'h5; // @[DivSqrtRecF64_mulAddZ31.scala 503:41]
  wire  zLinPiece_6_A4_div = cyc_A4_div & fractB_S[51:49] == 3'h6; // @[DivSqrtRecF64_mulAddZ31.scala 504:41]
  wire  zLinPiece_7_A4_div = cyc_A4_div & fractB_S[51:49] == 3'h7; // @[DivSqrtRecF64_mulAddZ31.scala 505:41]
  wire [8:0] _T_569 = zLinPiece_0_A4_div ? 9'h1c7 : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 507:12]
  wire [8:0] _T_572 = zLinPiece_1_A4_div ? 9'h16c : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 508:12]
  wire [8:0] _T_573 = _T_569 | _T_572; // @[DivSqrtRecF64_mulAddZ31.scala 507:59]
  wire [8:0] _T_576 = zLinPiece_2_A4_div ? 9'h12a : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 509:12]
  wire [8:0] _T_577 = _T_573 | _T_576; // @[DivSqrtRecF64_mulAddZ31.scala 508:59]
  wire [8:0] _T_580 = zLinPiece_3_A4_div ? 9'hf8 : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 510:12]
  wire [8:0] _T_581 = _T_577 | _T_580; // @[DivSqrtRecF64_mulAddZ31.scala 509:59]
  wire [8:0] _T_584 = zLinPiece_4_A4_div ? 9'hd2 : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 511:12]
  wire [8:0] _T_585 = _T_581 | _T_584; // @[DivSqrtRecF64_mulAddZ31.scala 510:59]
  wire [8:0] _T_588 = zLinPiece_5_A4_div ? 9'hb4 : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 512:12]
  wire [8:0] _T_589 = _T_585 | _T_588; // @[DivSqrtRecF64_mulAddZ31.scala 511:59]
  wire [8:0] _T_592 = zLinPiece_6_A4_div ? 9'h9c : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 513:12]
  wire [8:0] _T_593 = _T_589 | _T_592; // @[DivSqrtRecF64_mulAddZ31.scala 512:59]
  wire [8:0] _T_596 = zLinPiece_7_A4_div ? 9'h89 : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 514:12]
  wire [8:0] zK1_A4_div = _T_593 | _T_596; // @[DivSqrtRecF64_mulAddZ31.scala 513:59]
  wire [11:0] _T_600 = zLinPiece_0_A4_div ? 12'h1c : 12'h0; // @[DivSqrtRecF64_mulAddZ31.scala 516:12]
  wire [11:0] _T_604 = zLinPiece_1_A4_div ? 12'h3a2 : 12'h0; // @[DivSqrtRecF64_mulAddZ31.scala 517:12]
  wire [11:0] _T_605 = _T_600 | _T_604; // @[DivSqrtRecF64_mulAddZ31.scala 516:61]
  wire [11:0] _T_609 = zLinPiece_2_A4_div ? 12'h675 : 12'h0; // @[DivSqrtRecF64_mulAddZ31.scala 518:12]
  wire [11:0] _T_610 = _T_605 | _T_609; // @[DivSqrtRecF64_mulAddZ31.scala 517:61]
  wire [11:0] _T_614 = zLinPiece_3_A4_div ? 12'h8c6 : 12'h0; // @[DivSqrtRecF64_mulAddZ31.scala 519:12]
  wire [11:0] _T_615 = _T_610 | _T_614; // @[DivSqrtRecF64_mulAddZ31.scala 518:61]
  wire [11:0] _T_619 = zLinPiece_4_A4_div ? 12'hab4 : 12'h0; // @[DivSqrtRecF64_mulAddZ31.scala 520:12]
  wire [11:0] _T_620 = _T_615 | _T_619; // @[DivSqrtRecF64_mulAddZ31.scala 519:61]
  wire [11:0] _T_624 = zLinPiece_5_A4_div ? 12'hc56 : 12'h0; // @[DivSqrtRecF64_mulAddZ31.scala 521:12]
  wire [11:0] _T_625 = _T_620 | _T_624; // @[DivSqrtRecF64_mulAddZ31.scala 520:61]
  wire [11:0] _T_629 = zLinPiece_6_A4_div ? 12'hdbd : 12'h0; // @[DivSqrtRecF64_mulAddZ31.scala 522:12]
  wire [11:0] _T_630 = _T_625 | _T_629; // @[DivSqrtRecF64_mulAddZ31.scala 521:61]
  wire [11:0] _T_634 = zLinPiece_7_A4_div ? 12'hef4 : 12'h0; // @[DivSqrtRecF64_mulAddZ31.scala 523:12]
  wire [11:0] zComplFractK0_A4_div = _T_630 | _T_634; // @[DivSqrtRecF64_mulAddZ31.scala 522:61]
  wire [51:0] zFractB_A7_sqrt = cyc_A7_sqrt ? fractB_S : 52'h0; // @[DivSqrtRecF64_mulAddZ31.scala 525:30]
  wire  _T_639 = cyc_A7_sqrt & ~expB_S[0]; // @[DivSqrtRecF64_mulAddZ31.scala 527:44]
  wire  _T_642 = ~fractB_S[51]; // @[DivSqrtRecF64_mulAddZ31.scala 527:62]
  wire  zQuadPiece_0_A7_sqrt = cyc_A7_sqrt & ~expB_S[0] & ~fractB_S[51]; // @[DivSqrtRecF64_mulAddZ31.scala 527:59]
  wire  zQuadPiece_1_A7_sqrt = _T_639 & fractB_S[51]; // @[DivSqrtRecF64_mulAddZ31.scala 528:59]
  wire  _T_649 = cyc_A7_sqrt & expB_S[0]; // @[DivSqrtRecF64_mulAddZ31.scala 529:44]
  wire  zQuadPiece_2_A7_sqrt = cyc_A7_sqrt & expB_S[0] & _T_642; // @[DivSqrtRecF64_mulAddZ31.scala 529:59]
  wire  zQuadPiece_3_A7_sqrt = _T_649 & fractB_S[51]; // @[DivSqrtRecF64_mulAddZ31.scala 530:59]
  wire [8:0] _T_658 = zQuadPiece_0_A7_sqrt ? 9'h1c8 : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 532:12]
  wire [8:0] _T_661 = zQuadPiece_1_A7_sqrt ? 9'hc1 : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 533:12]
  wire [8:0] _T_662 = _T_658 | _T_661; // @[DivSqrtRecF64_mulAddZ31.scala 532:61]
  wire [8:0] _T_665 = zQuadPiece_2_A7_sqrt ? 9'h143 : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 534:12]
  wire [8:0] _T_666 = _T_662 | _T_665; // @[DivSqrtRecF64_mulAddZ31.scala 533:61]
  wire [8:0] _T_669 = zQuadPiece_3_A7_sqrt ? 9'h89 : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 535:12]
  wire [8:0] zK2_A7_sqrt = _T_666 | _T_669; // @[DivSqrtRecF64_mulAddZ31.scala 534:61]
  wire [9:0] _T_673 = zQuadPiece_0_A7_sqrt ? 10'h2f : 10'h0; // @[DivSqrtRecF64_mulAddZ31.scala 537:12]
  wire [9:0] _T_677 = zQuadPiece_1_A7_sqrt ? 10'h1df : 10'h0; // @[DivSqrtRecF64_mulAddZ31.scala 538:12]
  wire [9:0] _T_678 = _T_673 | _T_677; // @[DivSqrtRecF64_mulAddZ31.scala 537:63]
  wire [9:0] _T_682 = zQuadPiece_2_A7_sqrt ? 10'h14d : 10'h0; // @[DivSqrtRecF64_mulAddZ31.scala 539:12]
  wire [9:0] _T_683 = _T_678 | _T_682; // @[DivSqrtRecF64_mulAddZ31.scala 538:63]
  wire [9:0] _T_687 = zQuadPiece_3_A7_sqrt ? 10'h27e : 10'h0; // @[DivSqrtRecF64_mulAddZ31.scala 540:12]
  wire [9:0] zComplK1_A7_sqrt = _T_683 | _T_687; // @[DivSqrtRecF64_mulAddZ31.scala 539:63]
  wire  _T_691 = cyc_A6_sqrt & ~exp_PA[0]; // @[DivSqrtRecF64_mulAddZ31.scala 542:44]
  wire  _T_694 = ~sigB_PA[51]; // @[DivSqrtRecF64_mulAddZ31.scala 542:62]
  wire  zQuadPiece_0_A6_sqrt = cyc_A6_sqrt & ~exp_PA[0] & ~sigB_PA[51]; // @[DivSqrtRecF64_mulAddZ31.scala 542:59]
  wire  zQuadPiece_1_A6_sqrt = _T_691 & sigB_PA[51]; // @[DivSqrtRecF64_mulAddZ31.scala 543:59]
  wire  _T_701 = cyc_A6_sqrt & exp_PA[0]; // @[DivSqrtRecF64_mulAddZ31.scala 544:44]
  wire  zQuadPiece_2_A6_sqrt = cyc_A6_sqrt & exp_PA[0] & _T_694; // @[DivSqrtRecF64_mulAddZ31.scala 544:59]
  wire  zQuadPiece_3_A6_sqrt = _T_701 & sigB_PA[51]; // @[DivSqrtRecF64_mulAddZ31.scala 545:59]
  wire [12:0] _T_711 = zQuadPiece_0_A6_sqrt ? 13'h1a : 13'h0; // @[DivSqrtRecF64_mulAddZ31.scala 547:12]
  wire [12:0] _T_715 = zQuadPiece_1_A6_sqrt ? 13'hbca : 13'h0; // @[DivSqrtRecF64_mulAddZ31.scala 548:12]
  wire [12:0] _T_716 = _T_711 | _T_715; // @[DivSqrtRecF64_mulAddZ31.scala 547:64]
  wire [12:0] _T_720 = zQuadPiece_2_A6_sqrt ? 13'h12d3 : 13'h0; // @[DivSqrtRecF64_mulAddZ31.scala 549:12]
  wire [12:0] _T_721 = _T_716 | _T_720; // @[DivSqrtRecF64_mulAddZ31.scala 548:64]
  wire [12:0] _T_725 = zQuadPiece_3_A6_sqrt ? 13'h1b17 : 13'h0; // @[DivSqrtRecF64_mulAddZ31.scala 550:12]
  wire [12:0] zComplFractK0_A6_sqrt = _T_721 | _T_725; // @[DivSqrtRecF64_mulAddZ31.scala 549:64]
  wire [8:0] _T_727 = zFractB_A4_div[48:40] | zK2_A7_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 553:32]
  wire  _T_729 = ~cyc_S; // @[DivSqrtRecF64_mulAddZ31.scala 554:17]
  wire [8:0] _T_731 = ~cyc_S ? nextMulAdd9A_A : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 554:16]
  wire [8:0] mulAdd9A_A = _T_727 | _T_731; // @[DivSqrtRecF64_mulAddZ31.scala 553:46]
  wire [8:0] _T_733 = zK1_A4_div | zFractB_A7_sqrt[50:42]; // @[DivSqrtRecF64_mulAddZ31.scala 556:20]
  wire [8:0] _T_737 = _T_729 ? nextMulAdd9B_A : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 557:16]
  wire [8:0] mulAdd9B_A = _T_733 | _T_737; // @[DivSqrtRecF64_mulAddZ31.scala 556:46]
  wire [9:0] _T_741 = cyc_A7_sqrt ? 10'h3ff : 10'h0; // @[Bitwise.scala 71:12]
  wire [19:0] _T_742 = {zComplK1_A7_sqrt,_T_741}; // @[Cat.scala 30:58]
  wire [5:0] _T_746 = cyc_A6_sqrt ? 6'h3f : 6'h0; // @[Bitwise.scala 71:12]
  wire [19:0] _T_748 = {cyc_A6_sqrt,zComplFractK0_A6_sqrt,_T_746}; // @[Cat.scala 30:58]
  wire [19:0] _T_749 = _T_742 | _T_748; // @[DivSqrtRecF64_mulAddZ31.scala 559:71]
  wire [7:0] _T_753 = cyc_A4_div ? 8'hff : 8'h0; // @[Bitwise.scala 71:12]
  wire [20:0] _T_755 = {cyc_A4_div,zComplFractK0_A4_div,_T_753}; // @[Cat.scala 30:58]
  wire [20:0] _GEN_55 = {{1'd0}, _T_749}; // @[DivSqrtRecF64_mulAddZ31.scala 560:71]
  wire [20:0] _T_756 = _GEN_55 | _T_755; // @[DivSqrtRecF64_mulAddZ31.scala 560:71]
  wire [18:0] _T_758 = {fractR0_A, 10'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 563:54]
  wire [19:0] _GEN_56 = {{1'd0}, _T_758}; // @[DivSqrtRecF64_mulAddZ31.scala 563:42]
  wire [19:0] _T_760 = 20'h40000 + _GEN_56; // @[DivSqrtRecF64_mulAddZ31.scala 563:42]
  wire [19:0] _T_762 = cyc_A5_sqrt ? _T_760 : 20'h0; // @[DivSqrtRecF64_mulAddZ31.scala 563:12]
  wire [20:0] _GEN_57 = {{1'd0}, _T_762}; // @[DivSqrtRecF64_mulAddZ31.scala 561:71]
  wire [20:0] _T_763 = _T_756 | _GEN_57; // @[DivSqrtRecF64_mulAddZ31.scala 561:71]
  wire [10:0] _T_770 = cyc_A4_sqrt & ~hiSqrR0_A_sqrt[9] ? 11'h400 : 11'h0; // @[DivSqrtRecF64_mulAddZ31.scala 564:12]
  wire [20:0] _GEN_58 = {{10'd0}, _T_770}; // @[DivSqrtRecF64_mulAddZ31.scala 563:70]
  wire [20:0] _T_771 = _T_763 | _GEN_58; // @[DivSqrtRecF64_mulAddZ31.scala 563:70]
  wire [20:0] _T_778 = sigB_PA[46:26] + 21'h400; // @[DivSqrtRecF64_mulAddZ31.scala 566:29]
  wire [20:0] _T_780 = cyc_A4_sqrt & hiSqrR0_A_sqrt[9] | cyc_A3_div ? _T_778 : 21'h0; // @[DivSqrtRecF64_mulAddZ31.scala 565:12]
  wire [20:0] _T_781 = _T_771 | _T_780; // @[DivSqrtRecF64_mulAddZ31.scala 564:71]
  wire [20:0] _T_784 = cyc_A3_sqrt | cyc_A2 ? partNegSigma0_A : 21'h0; // @[DivSqrtRecF64_mulAddZ31.scala 569:12]
  wire [20:0] _T_785 = _T_781 | _T_784; // @[DivSqrtRecF64_mulAddZ31.scala 568:11]
  wire [24:0] _T_786 = {fractR0_A, 16'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 570:45]
  wire [24:0] _T_788 = cyc_A1_sqrt ? _T_786 : 25'h0; // @[DivSqrtRecF64_mulAddZ31.scala 570:12]
  wire [24:0] _GEN_59 = {{4'd0}, _T_785}; // @[DivSqrtRecF64_mulAddZ31.scala 569:62]
  wire [24:0] _T_789 = _GEN_59 | _T_788; // @[DivSqrtRecF64_mulAddZ31.scala 569:62]
  wire [23:0] _T_790 = {fractR0_A, 15'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 571:45]
  wire [23:0] _T_792 = cyc_A1_div ? _T_790 : 24'h0; // @[DivSqrtRecF64_mulAddZ31.scala 571:12]
  wire [24:0] _GEN_60 = {{1'd0}, _T_792}; // @[DivSqrtRecF64_mulAddZ31.scala 570:62]
  wire [24:0] mulAdd9C_A = _T_789 | _GEN_60; // @[DivSqrtRecF64_mulAddZ31.scala 570:62]
  wire [17:0] _T_793 = mulAdd9A_A * mulAdd9B_A; // @[DivSqrtRecF64_mulAddZ31.scala 573:20]
  wire [18:0] _T_796 = {1'h0,mulAdd9C_A[17:0]}; // @[Cat.scala 30:58]
  wire [18:0] _GEN_61 = {{1'd0}, _T_793}; // @[DivSqrtRecF64_mulAddZ31.scala 573:33]
  wire [18:0] loMulAdd9Out_A = _GEN_61 + _T_796; // @[DivSqrtRecF64_mulAddZ31.scala 573:33]
  wire [6:0] _T_802 = mulAdd9C_A[24:18] + 7'h1; // @[DivSqrtRecF64_mulAddZ31.scala 576:36]
  wire [6:0] _T_804 = loMulAdd9Out_A[18] ? _T_802 : mulAdd9C_A[24:18]; // @[DivSqrtRecF64_mulAddZ31.scala 575:16]
  wire [24:0] mulAdd9Out_A = {_T_804,loMulAdd9Out_A[17:0]}; // @[Cat.scala 30:58]
  wire [24:0] _T_808 = ~mulAdd9Out_A; // @[DivSqrtRecF64_mulAddZ31.scala 584:13]
  wire [14:0] _T_811 = cyc_A6_sqrt & mulAdd9Out_A[19] ? _T_808[24:10] : 15'h0; // @[DivSqrtRecF64_mulAddZ31.scala 583:12]
  wire [8:0] zFractR0_A6_sqrt = _T_811[8:0]; // @[DivSqrtRecF64_mulAddZ31.scala 586:10]
  wire [25:0] _T_813 = {mulAdd9Out_A, 1'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 590:52]
  wire [25:0] sqrR0_A5_sqrt = exp_PA[0] ? _T_813 : {{1'd0}, mulAdd9Out_A}; // @[DivSqrtRecF64_mulAddZ31.scala 590:28]
  wire [13:0] _T_819 = cyc_A4_div & mulAdd9Out_A[20] ? _T_808[24:11] : 14'h0; // @[DivSqrtRecF64_mulAddZ31.scala 592:12]
  wire [8:0] zFractR0_A4_div = _T_819[8:0]; // @[DivSqrtRecF64_mulAddZ31.scala 595:10]
  wire [22:0] _T_825 = cyc_A2 & mulAdd9Out_A[11] ? _T_808[24:2] : 23'h0; // @[DivSqrtRecF64_mulAddZ31.scala 598:12]
  wire [8:0] zSigma0_A2 = _T_825[8:0]; // @[DivSqrtRecF64_mulAddZ31.scala 598:67]
  wire [15:0] _T_828 = sqrtOp_PA ? {{1'd0}, mulAdd9Out_A[24:10]} : mulAdd9Out_A[24:9]; // @[DivSqrtRecF64_mulAddZ31.scala 601:12]
  wire [14:0] fractR1_A1 = _T_828[14:0]; // @[DivSqrtRecF64_mulAddZ31.scala 601:58]
  wire [15:0] r1_A1 = {1'h1,fractR1_A1}; // @[Cat.scala 30:58]
  wire [16:0] _T_831 = {r1_A1, 1'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 603:43]
  wire [16:0] ER1_A1_sqrt = exp_PA[0] ? _T_831 : {{1'd0}, r1_A1}; // @[DivSqrtRecF64_mulAddZ31.scala 603:26]
  wire [8:0] _T_833 = zFractR0_A6_sqrt | zFractR0_A4_div; // @[DivSqrtRecF64_mulAddZ31.scala 606:39]
  wire [15:0] _GEN_38 = cyc_A5_sqrt ? sqrR0_A5_sqrt[25:10] : {{6'd0}, hiSqrR0_A_sqrt}; // @[DivSqrtRecF64_mulAddZ31.scala 609:24 DivSqrtRecF64_mulAddZ31.scala 610:24 DivSqrtRecF64_mulAddZ31.scala 124:30]
  wire [24:0] _T_837 = cyc_A4_sqrt ? mulAdd9Out_A : {{9'd0}, mulAdd9Out_A[24:9]}; // @[DivSqrtRecF64_mulAddZ31.scala 616:16]
  wire  _T_841 = cyc_A7_sqrt | cyc_A6_sqrt | cyc_A5_sqrt | cyc_A4; // @[DivSqrtRecF64_mulAddZ31.scala 620:51]
  wire  _T_843 = cyc_A7_sqrt | cyc_A6_sqrt | cyc_A5_sqrt | cyc_A4 | cyc_A3 | cyc_A2; // @[DivSqrtRecF64_mulAddZ31.scala 620:71]
  wire [13:0] _T_847 = cyc_A7_sqrt ? _T_808[24:11] : 14'h0; // @[DivSqrtRecF64_mulAddZ31.scala 623:16]
  wire [13:0] _GEN_62 = {{5'd0}, zFractR0_A6_sqrt}; // @[DivSqrtRecF64_mulAddZ31.scala 623:68]
  wire [13:0] _T_848 = _T_847 | _GEN_62; // @[DivSqrtRecF64_mulAddZ31.scala 623:68]
  wire [8:0] _T_851 = cyc_A4_sqrt ? sigB_PA[43:35] : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 625:16]
  wire [13:0] _GEN_63 = {{5'd0}, _T_851}; // @[DivSqrtRecF64_mulAddZ31.scala 624:68]
  wire [13:0] _T_852 = _T_848 | _GEN_63; // @[DivSqrtRecF64_mulAddZ31.scala 624:68]
  wire [13:0] _GEN_64 = {{5'd0}, zFractB_A4_div[43:35]}; // @[DivSqrtRecF64_mulAddZ31.scala 625:68]
  wire [13:0] _T_854 = _T_852 | _GEN_64; // @[DivSqrtRecF64_mulAddZ31.scala 625:68]
  wire [8:0] _T_858 = cyc_A5_sqrt | cyc_A3 ? sigB_PA[52:44] : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 627:16]
  wire [13:0] _GEN_65 = {{5'd0}, _T_858}; // @[DivSqrtRecF64_mulAddZ31.scala 626:68]
  wire [13:0] _T_859 = _T_854 | _GEN_65; // @[DivSqrtRecF64_mulAddZ31.scala 626:68]
  wire [13:0] _GEN_66 = {{5'd0}, zSigma0_A2}; // @[DivSqrtRecF64_mulAddZ31.scala 627:68]
  wire [13:0] _T_860 = _T_859 | _GEN_66; // @[DivSqrtRecF64_mulAddZ31.scala 627:68]
  wire [13:0] _GEN_40 = _T_843 ? _T_860 : {{5'd0}, nextMulAdd9A_A}; // @[DivSqrtRecF64_mulAddZ31.scala 621:7 DivSqrtRecF64_mulAddZ31.scala 622:24 DivSqrtRecF64_mulAddZ31.scala 126:30]
  wire [8:0] _T_866 = zFractB_A7_sqrt[50:42] | zFractR0_A6_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 632:73]
  wire [8:0] _T_869 = cyc_A5_sqrt ? sqrR0_A5_sqrt[9:1] : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 634:16]
  wire [8:0] _T_870 = _T_866 | _T_869; // @[DivSqrtRecF64_mulAddZ31.scala 633:73]
  wire [8:0] _T_871 = _T_870 | zFractR0_A4_div; // @[DivSqrtRecF64_mulAddZ31.scala 634:73]
  wire [8:0] _T_874 = cyc_A4_sqrt ? hiSqrR0_A_sqrt[8:0] : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 636:16]
  wire [8:0] _T_875 = _T_871 | _T_874; // @[DivSqrtRecF64_mulAddZ31.scala 635:73]
  wire [8:0] _T_878 = {1'h1,fractR0_A[8:1]}; // @[Cat.scala 30:58]
  wire [8:0] _T_880 = cyc_A2 ? _T_878 : 9'h0; // @[DivSqrtRecF64_mulAddZ31.scala 637:16]
  wire [8:0] _T_881 = _T_875 | _T_880; // @[DivSqrtRecF64_mulAddZ31.scala 636:73]
  wire  _T_882 = cyc_A1 | cyc_B7_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 647:16]
  wire  _T_886 = cyc_A1 | cyc_B7_sqrt | cyc_B6_div | cyc_B4 | cyc_B3 | cyc_C6_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 647:65]
  wire [52:0] _T_889 = {ER1_A1_sqrt, 36'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 650:51]
  wire [52:0] _T_891 = cyc_A1_sqrt ? _T_889 : 53'h0; // @[DivSqrtRecF64_mulAddZ31.scala 650:12]
  wire [52:0] _T_894 = cyc_B7_sqrt | cyc_A1_div ? sigB_PA : 53'h0; // @[DivSqrtRecF64_mulAddZ31.scala 651:12]
  wire [52:0] _T_895 = _T_891 | _T_894; // @[DivSqrtRecF64_mulAddZ31.scala 650:67]
  wire [52:0] _T_897 = cyc_B6_div ? sigA_PA : 53'h0; // @[DivSqrtRecF64_mulAddZ31.scala 652:12]
  wire [52:0] _T_898 = _T_895 | _T_897; // @[DivSqrtRecF64_mulAddZ31.scala 651:67]
  wire [45:0] _T_1019 = ~io_mulAddResult_3[90:45]; // @[DivSqrtRecF64_mulAddZ31.scala 702:31]
  wire [45:0] zSigma1_B4 = cyc_B4 ? _T_1019 : 46'h0; // @[DivSqrtRecF64_mulAddZ31.scala 702:22]
  wire [52:0] _GEN_67 = {{19'd0}, zSigma1_B4[45:12]}; // @[DivSqrtRecF64_mulAddZ31.scala 652:67]
  wire [52:0] _T_900 = _T_898 | _GEN_67; // @[DivSqrtRecF64_mulAddZ31.scala 652:67]
  wire [57:0] sigXNU_B3_CX = io_mulAddResult_3[104:47]; // @[DivSqrtRecF64_mulAddZ31.scala 704:38]
  wire [45:0] _T_904 = cyc_B3 | cyc_C6_sqrt ? sigXNU_B3_CX[57:12] : 46'h0; // @[DivSqrtRecF64_mulAddZ31.scala 655:12]
  wire [52:0] _GEN_68 = {{7'd0}, _T_904}; // @[DivSqrtRecF64_mulAddZ31.scala 653:67]
  wire [52:0] _T_905 = _T_900 | _GEN_68; // @[DivSqrtRecF64_mulAddZ31.scala 653:67]
  wire [45:0] _T_907 = {sigXN_C[57:25], 13'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 656:51]
  wire [45:0] _T_909 = cyc_C4_div ? _T_907 : 46'h0; // @[DivSqrtRecF64_mulAddZ31.scala 656:12]
  wire [52:0] _GEN_69 = {{7'd0}, _T_909}; // @[DivSqrtRecF64_mulAddZ31.scala 655:67]
  wire [52:0] _T_910 = _T_905 | _GEN_69; // @[DivSqrtRecF64_mulAddZ31.scala 655:67]
  wire [45:0] _T_911 = {u_C_sqrt, 15'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 657:44]
  wire [45:0] _T_913 = cyc_C4_sqrt ? _T_911 : 46'h0; // @[DivSqrtRecF64_mulAddZ31.scala 657:12]
  wire [52:0] _GEN_70 = {{7'd0}, _T_913}; // @[DivSqrtRecF64_mulAddZ31.scala 656:67]
  wire [52:0] _T_914 = _T_910 | _GEN_70; // @[DivSqrtRecF64_mulAddZ31.scala 656:67]
  wire [52:0] _T_916 = cyc_C1_div ? sigB_PC : 53'h0; // @[DivSqrtRecF64_mulAddZ31.scala 658:12]
  wire [52:0] _T_917 = _T_914 | _T_916; // @[DivSqrtRecF64_mulAddZ31.scala 657:67]
  wire [53:0] _T_1042 = ~io_mulAddResult_3[104:51]; // @[DivSqrtRecF64_mulAddZ31.scala 716:26]
  wire [53:0] zComplSigT_C1_sqrt = cyc_C1_sqrt ? _T_1042 : 54'h0; // @[DivSqrtRecF64_mulAddZ31.scala 716:12]
  wire [53:0] _GEN_71 = {{1'd0}, _T_917}; // @[DivSqrtRecF64_mulAddZ31.scala 658:67]
  wire  _T_922 = _T_882 | cyc_B6_sqrt | cyc_B4 | cyc_C6_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 661:56]
  wire [51:0] _T_925 = {r1_A1, 36'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 664:31]
  wire [51:0] _T_927 = cyc_A1 ? _T_925 : 52'h0; // @[DivSqrtRecF64_mulAddZ31.scala 664:12]
  wire [50:0] _T_928 = {ESqrR1_B_sqrt, 19'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 665:39]
  wire [50:0] _T_930 = cyc_B7_sqrt ? _T_928 : 51'h0; // @[DivSqrtRecF64_mulAddZ31.scala 665:12]
  wire [51:0] _GEN_72 = {{1'd0}, _T_930}; // @[DivSqrtRecF64_mulAddZ31.scala 664:55]
  wire [51:0] _T_931 = _T_927 | _GEN_72; // @[DivSqrtRecF64_mulAddZ31.scala 664:55]
  wire [52:0] _T_932 = {ER1_B_sqrt, 36'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 666:36]
  wire [52:0] _T_934 = cyc_B6_sqrt ? _T_932 : 53'h0; // @[DivSqrtRecF64_mulAddZ31.scala 666:12]
  wire [52:0] _GEN_73 = {{1'd0}, _T_931}; // @[DivSqrtRecF64_mulAddZ31.scala 665:55]
  wire [52:0] _T_935 = _GEN_73 | _T_934; // @[DivSqrtRecF64_mulAddZ31.scala 665:55]
  wire [52:0] _GEN_74 = {{7'd0}, zSigma1_B4}; // @[DivSqrtRecF64_mulAddZ31.scala 666:55]
  wire [52:0] _T_936 = _T_935 | _GEN_74; // @[DivSqrtRecF64_mulAddZ31.scala 666:55]
  wire [29:0] _T_939 = cyc_C6_sqrt ? sqrSigma1_C[30:1] : 30'h0; // @[DivSqrtRecF64_mulAddZ31.scala 668:12]
  wire [52:0] _GEN_75 = {{23'd0}, _T_939}; // @[DivSqrtRecF64_mulAddZ31.scala 667:55]
  wire [52:0] _T_940 = _T_936 | _GEN_75; // @[DivSqrtRecF64_mulAddZ31.scala 667:55]
  wire [32:0] _T_942 = cyc_C4 ? sqrSigma1_C : 33'h0; // @[DivSqrtRecF64_mulAddZ31.scala 669:12]
  wire [52:0] _GEN_76 = {{20'd0}, _T_942}; // @[DivSqrtRecF64_mulAddZ31.scala 668:55]
  wire [52:0] _T_943 = _T_940 | _GEN_76; // @[DivSqrtRecF64_mulAddZ31.scala 668:55]
  wire  E_C1_div = ~io_mulAddResult_3[104]; // @[DivSqrtRecF64_mulAddZ31.scala 705:20]
  wire [53:0] _T_1032 = cyc_C1_div & ~E_C1_div | cyc_C1_sqrt ? _T_1042 : 54'h0; // @[DivSqrtRecF64_mulAddZ31.scala 707:12]
  wire [52:0] _T_1036 = ~io_mulAddResult_3[102:50]; // @[DivSqrtRecF64_mulAddZ31.scala 712:29]
  wire [53:0] _T_1037 = {1'h0,_T_1036}; // @[Cat.scala 30:58]
  wire [53:0] _T_1039 = cyc_C1_div & E_C1_div ? _T_1037 : 54'h0; // @[DivSqrtRecF64_mulAddZ31.scala 711:12]
  wire [53:0] zComplSigT_C1 = _T_1032 | _T_1039; // @[DivSqrtRecF64_mulAddZ31.scala 710:11]
  wire [53:0] _GEN_77 = {{1'd0}, _T_943}; // @[DivSqrtRecF64_mulAddZ31.scala 669:55]
  wire  _T_947 = cyc_A4 | cyc_A3_div | cyc_A1_div | cyc_B10_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 672:48]
  wire  _T_951 = _T_947 | cyc_B9_sqrt | cyc_B7_sqrt | cyc_B6 | cyc_B5_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 673:70]
  wire  _T_955 = _T_951 | cyc_B3_sqrt | cyc_B2_div | cyc_B1_sqrt | cyc_C4; // @[DivSqrtRecF64_mulAddZ31.scala 674:73]
  wire  _T_957 = cyc_A3 | cyc_A2_div | cyc_B9_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 676:34]
  wire  _T_961 = _T_957 | cyc_B8_sqrt | cyc_B6 | cyc_B5 | cyc_B4_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 677:64]
  wire  _T_965 = _T_961 | cyc_B2_sqrt | cyc_B1_div | cyc_C6_sqrt | cyc_C3; // @[DivSqrtRecF64_mulAddZ31.scala 678:73]
  wire  _T_967 = cyc_A2 | cyc_A1_div | cyc_B8_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 680:34]
  wire  _T_971 = _T_967 | cyc_B7_sqrt | cyc_B5 | cyc_B4 | cyc_B3_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 681:64]
  wire  _T_974 = _T_971 | cyc_B1_sqrt | cyc_C5 | cyc_C2; // @[DivSqrtRecF64_mulAddZ31.scala 682:54]
  wire  _T_976 = io_latchMulAddA_0 | cyc_B6 | cyc_B2_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 684:41]
  wire [1:0] _T_977 = {_T_974,_T_976}; // @[Cat.scala 30:58]
  wire [1:0] _T_978 = {_T_955,_T_965}; // @[Cat.scala 30:58]
  wire [104:0] _T_980 = {sigX1_B, 47'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 688:45]
  wire [104:0] _T_982 = cyc_B1 ? _T_980 : 105'h0; // @[DivSqrtRecF64_mulAddZ31.scala 688:12]
  wire [103:0] _T_983 = {sigX1_B, 46'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 689:45]
  wire [103:0] _T_985 = cyc_C6_sqrt ? _T_983 : 104'h0; // @[DivSqrtRecF64_mulAddZ31.scala 689:12]
  wire [104:0] _GEN_78 = {{1'd0}, _T_985}; // @[DivSqrtRecF64_mulAddZ31.scala 688:64]
  wire [104:0] _T_986 = _T_982 | _GEN_78; // @[DivSqrtRecF64_mulAddZ31.scala 688:64]
  wire [104:0] _T_988 = {sigXN_C, 47'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 690:45]
  wire [104:0] _T_990 = cyc_C4_sqrt | cyc_C2 ? _T_988 : 105'h0; // @[DivSqrtRecF64_mulAddZ31.scala 690:12]
  wire [104:0] _T_991 = _T_986 | _T_990; // @[DivSqrtRecF64_mulAddZ31.scala 689:64]
  wire  _T_993 = ~E_E_div; // @[DivSqrtRecF64_mulAddZ31.scala 691:27]
  wire [53:0] _T_995 = {fractA_0_PC, 53'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 691:49]
  wire [53:0] _T_997 = cyc_E3_div & ~E_E_div ? _T_995 : 54'h0; // @[DivSqrtRecF64_mulAddZ31.scala 691:12]
  wire [104:0] _GEN_79 = {{51'd0}, _T_997}; // @[DivSqrtRecF64_mulAddZ31.scala 690:64]
  wire [104:0] _T_998 = _T_991 | _GEN_79; // @[DivSqrtRecF64_mulAddZ31.scala 690:64]
  wire [1:0] _T_1002 = {sigB_PC[0],1'h0}; // @[Cat.scala 30:58]
  wire  _T_1005 = sigB_PC[1] ^ sigB_PC[0]; // @[DivSqrtRecF64_mulAddZ31.scala 695:33]
  wire [1:0] _T_1007 = {_T_1005,sigB_PC[0]}; // @[Cat.scala 30:58]
  wire [1:0] _T_1008 = exp_PC[0] ? _T_1002 : _T_1007; // @[DivSqrtRecF64_mulAddZ31.scala 693:17]
  wire  _T_1010 = ~extraT_E; // @[DivSqrtRecF64_mulAddZ31.scala 696:22]
  wire [1:0] _T_1012 = {_T_1010,1'h0}; // @[Cat.scala 30:58]
  wire [1:0] _T_1013 = _T_1008 ^ _T_1012; // @[DivSqrtRecF64_mulAddZ31.scala 696:16]
  wire [55:0] _T_1014 = {_T_1013, 54'h0}; // @[DivSqrtRecF64_mulAddZ31.scala 697:14]
  wire [55:0] _T_1016 = cyc_E3_sqrt ? _T_1014 : 56'h0; // @[DivSqrtRecF64_mulAddZ31.scala 692:12]
  wire [104:0] _GEN_80 = {{49'd0}, _T_1016}; // @[DivSqrtRecF64_mulAddZ31.scala 691:64]
  wire [31:0] ESqrR1_B8_sqrt = io_mulAddResult_3[103:72]; // @[DivSqrtRecF64_mulAddZ31.scala 701:43]
  wire [32:0] sqrSigma1_B1 = io_mulAddResult_3[79:47]; // @[DivSqrtRecF64_mulAddZ31.scala 703:41]
  wire [53:0] sigT_C1 = ~zComplSigT_C1; // @[DivSqrtRecF64_mulAddZ31.scala 720:19]
  wire [55:0] remT_E2 = io_mulAddResult_3[55:0]; // @[DivSqrtRecF64_mulAddZ31.scala 721:36]
  wire  _T_1061 = _T_390 | remT_E2[55:54] == 2'h0; // @[DivSqrtRecF64_mulAddZ31.scala 749:30]
  wire  _T_1062 = remT_E2[53:0] == 54'h0 & _T_1061; // @[DivSqrtRecF64_mulAddZ31.scala 748:42]
  wire [13:0] _T_1067 = _T_390 & E_E_div ? exp_PC : 14'h0; // @[DivSqrtRecF64_mulAddZ31.scala 755:12]
  wire [13:0] _T_1074 = _T_390 & _T_993 ? expP1_PC : 14'h0; // @[DivSqrtRecF64_mulAddZ31.scala 756:12]
  wire [13:0] _T_1075 = _T_1067 | _T_1074; // @[DivSqrtRecF64_mulAddZ31.scala 755:76]
  wire [12:0] _T_1079 = exp_PC[13:1] + 13'h400; // @[DivSqrtRecF64_mulAddZ31.scala 757:47]
  wire [12:0] _T_1081 = sqrtOp_PC ? _T_1079 : 13'h0; // @[DivSqrtRecF64_mulAddZ31.scala 757:12]
  wire [13:0] _GEN_81 = {{1'd0}, _T_1081}; // @[DivSqrtRecF64_mulAddZ31.scala 756:76]
  wire [13:0] sExpX_E = _T_1075 | _GEN_81; // @[DivSqrtRecF64_mulAddZ31.scala 756:76]
  wire [12:0] posExpX_E = sExpX_E[12:0]; // @[DivSqrtRecF64_mulAddZ31.scala 759:28]
  wire [12:0] _T_1082 = ~posExpX_E; // @[primitives.scala 50:21]
  wire [64:0] _T_1102 = 65'sh10000000000000000 >>> _T_1082[5:0]; // @[primitives.scala 68:52]
  wire [31:0] _T_1110 = {{16'd0}, _T_1102[45:30]}; // @[Bitwise.scala 102:31]
  wire [31:0] _T_1112 = {_T_1102[29:14], 16'h0}; // @[Bitwise.scala 102:65]
  wire [31:0] _T_1114 = _T_1112 & 32'hffff0000; // @[Bitwise.scala 102:75]
  wire [31:0] _T_1115 = _T_1110 | _T_1114; // @[Bitwise.scala 102:39]
  wire [31:0] _GEN_82 = {{8'd0}, _T_1115[31:8]}; // @[Bitwise.scala 102:31]
  wire [31:0] _T_1120 = _GEN_82 & 32'hff00ff; // @[Bitwise.scala 102:31]
  wire [31:0] _T_1122 = {_T_1115[23:0], 8'h0}; // @[Bitwise.scala 102:65]
  wire [31:0] _T_1124 = _T_1122 & 32'hff00ff00; // @[Bitwise.scala 102:75]
  wire [31:0] _T_1125 = _T_1120 | _T_1124; // @[Bitwise.scala 102:39]
  wire [31:0] _GEN_83 = {{4'd0}, _T_1125[31:4]}; // @[Bitwise.scala 102:31]
  wire [31:0] _T_1130 = _GEN_83 & 32'hf0f0f0f; // @[Bitwise.scala 102:31]
  wire [31:0] _T_1132 = {_T_1125[27:0], 4'h0}; // @[Bitwise.scala 102:65]
  wire [31:0] _T_1134 = _T_1132 & 32'hf0f0f0f0; // @[Bitwise.scala 102:75]
  wire [31:0] _T_1135 = _T_1130 | _T_1134; // @[Bitwise.scala 102:39]
  wire [31:0] _GEN_84 = {{2'd0}, _T_1135[31:2]}; // @[Bitwise.scala 102:31]
  wire [31:0] _T_1140 = _GEN_84 & 32'h33333333; // @[Bitwise.scala 102:31]
  wire [31:0] _T_1142 = {_T_1135[29:0], 2'h0}; // @[Bitwise.scala 102:65]
  wire [31:0] _T_1144 = _T_1142 & 32'hcccccccc; // @[Bitwise.scala 102:75]
  wire [31:0] _T_1145 = _T_1140 | _T_1144; // @[Bitwise.scala 102:39]
  wire [31:0] _GEN_85 = {{1'd0}, _T_1145[31:1]}; // @[Bitwise.scala 102:31]
  wire [31:0] _T_1150 = _GEN_85 & 32'h55555555; // @[Bitwise.scala 102:31]
  wire [31:0] _T_1152 = {_T_1145[30:0], 1'h0}; // @[Bitwise.scala 102:65]
  wire [31:0] _T_1154 = _T_1152 & 32'haaaaaaaa; // @[Bitwise.scala 102:75]
  wire [31:0] _T_1155 = _T_1150 | _T_1154; // @[Bitwise.scala 102:39]
  wire [15:0] _T_1163 = {{8'd0}, _T_1102[61:54]}; // @[Bitwise.scala 102:31]
  wire [15:0] _T_1165 = {_T_1102[53:46], 8'h0}; // @[Bitwise.scala 102:65]
  wire [15:0] _T_1167 = _T_1165 & 16'hff00; // @[Bitwise.scala 102:75]
  wire [15:0] _T_1168 = _T_1163 | _T_1167; // @[Bitwise.scala 102:39]
  wire [15:0] _GEN_86 = {{4'd0}, _T_1168[15:4]}; // @[Bitwise.scala 102:31]
  wire [15:0] _T_1173 = _GEN_86 & 16'hf0f; // @[Bitwise.scala 102:31]
  wire [15:0] _T_1175 = {_T_1168[11:0], 4'h0}; // @[Bitwise.scala 102:65]
  wire [15:0] _T_1177 = _T_1175 & 16'hf0f0; // @[Bitwise.scala 102:75]
  wire [15:0] _T_1178 = _T_1173 | _T_1177; // @[Bitwise.scala 102:39]
  wire [15:0] _GEN_87 = {{2'd0}, _T_1178[15:2]}; // @[Bitwise.scala 102:31]
  wire [15:0] _T_1183 = _GEN_87 & 16'h3333; // @[Bitwise.scala 102:31]
  wire [15:0] _T_1185 = {_T_1178[13:0], 2'h0}; // @[Bitwise.scala 102:65]
  wire [15:0] _T_1187 = _T_1185 & 16'hcccc; // @[Bitwise.scala 102:75]
  wire [15:0] _T_1188 = _T_1183 | _T_1187; // @[Bitwise.scala 102:39]
  wire [15:0] _GEN_88 = {{1'd0}, _T_1188[15:1]}; // @[Bitwise.scala 102:31]
  wire [15:0] _T_1193 = _GEN_88 & 16'h5555; // @[Bitwise.scala 102:31]
  wire [15:0] _T_1195 = {_T_1188[14:0], 1'h0}; // @[Bitwise.scala 102:65]
  wire [15:0] _T_1197 = _T_1195 & 16'haaaa; // @[Bitwise.scala 102:75]
  wire [15:0] _T_1198 = _T_1193 | _T_1197; // @[Bitwise.scala 102:39]
  wire [49:0] _T_1204 = {_T_1155,_T_1198,_T_1102[62],_T_1102[63]}; // @[Cat.scala 30:58]
  wire [49:0] _T_1205 = ~_T_1204; // @[primitives.scala 65:36]
  wire [49:0] _T_1206 = _T_1082[6] ? 50'h0 : _T_1205; // @[primitives.scala 65:21]
  wire [49:0] _T_1207 = ~_T_1206; // @[primitives.scala 65:17]
  wire [49:0] _T_1208 = ~_T_1207; // @[primitives.scala 65:36]
  wire [49:0] _T_1209 = _T_1082[7] ? 50'h0 : _T_1208; // @[primitives.scala 65:21]
  wire [49:0] _T_1210 = ~_T_1209; // @[primitives.scala 65:17]
  wire [49:0] _T_1211 = ~_T_1210; // @[primitives.scala 65:36]
  wire [49:0] _T_1212 = _T_1082[8] ? 50'h0 : _T_1211; // @[primitives.scala 65:21]
  wire [49:0] _T_1213 = ~_T_1212; // @[primitives.scala 65:17]
  wire [49:0] _T_1214 = ~_T_1213; // @[primitives.scala 65:36]
  wire [49:0] _T_1215 = _T_1082[9] ? 50'h0 : _T_1214; // @[primitives.scala 65:21]
  wire [49:0] _T_1216 = ~_T_1215; // @[primitives.scala 65:17]
  wire [52:0] _T_1218 = {_T_1216,3'h7}; // @[Cat.scala 30:58]
  wire [2:0] _T_1235 = {_T_1102[0],_T_1102[1],_T_1102[2]}; // @[Cat.scala 30:58]
  wire [2:0] _T_1237 = _T_1082[6] ? _T_1235 : 3'h0; // @[primitives.scala 59:20]
  wire [2:0] _T_1239 = _T_1082[7] ? _T_1237 : 3'h0; // @[primitives.scala 59:20]
  wire [2:0] _T_1241 = _T_1082[8] ? _T_1239 : 3'h0; // @[primitives.scala 59:20]
  wire [2:0] _T_1243 = _T_1082[9] ? _T_1241 : 3'h0; // @[primitives.scala 59:20]
  wire [52:0] _T_1244 = _T_1082[10] ? _T_1218 : {{50'd0}, _T_1243}; // @[primitives.scala 61:20]
  wire [52:0] _T_1246 = _T_1082[11] ? _T_1244 : 53'h0; // @[primitives.scala 59:20]
  wire [52:0] roundMask_E = _T_1082[12] ? _T_1246 : 53'h0; // @[primitives.scala 59:20]
  wire [53:0] _T_1249 = {1'h0,roundMask_E}; // @[Cat.scala 30:58]
  wire [53:0] _T_1250 = ~_T_1249; // @[DivSqrtRecF64_mulAddZ31.scala 763:9]
  wire [53:0] _T_1252 = {roundMask_E,1'h1}; // @[Cat.scala 30:58]
  wire [53:0] incrPosMask_E = _T_1250 & _T_1252; // @[DivSqrtRecF64_mulAddZ31.scala 763:39]
  wire [52:0] _T_1254 = sigT_E & incrPosMask_E[53:1]; // @[DivSqrtRecF64_mulAddZ31.scala 765:36]
  wire  hiRoundPosBitT_E = _T_1254 != 53'h0; // @[DivSqrtRecF64_mulAddZ31.scala 765:56]
  wire [52:0] _GEN_89 = {{1'd0}, roundMask_E[52:1]}; // @[DivSqrtRecF64_mulAddZ31.scala 766:42]
  wire [52:0] _T_1259 = ~sigT_E; // @[DivSqrtRecF64_mulAddZ31.scala 767:34]
  wire [52:0] _T_1261 = _T_1259 & _GEN_89; // @[DivSqrtRecF64_mulAddZ31.scala 767:42]
  wire  all1sHiRoundExtraT_E = _T_1261 == 53'h0; // @[DivSqrtRecF64_mulAddZ31.scala 767:60]
  wire  _T_1265 = ~roundMask_E[0]; // @[DivSqrtRecF64_mulAddZ31.scala 769:10]
  wire  all1sHiRoundT_E = (~roundMask_E[0] | hiRoundPosBitT_E) & all1sHiRoundExtraT_E; // @[DivSqrtRecF64_mulAddZ31.scala 769:48]
  wire [54:0] _T_1268 = {{2'd0}, sigT_E}; // @[DivSqrtRecF64_mulAddZ31.scala 773:33]
  wire [53:0] _GEN_91 = {{53'd0}, roundMagUp_PC}; // @[DivSqrtRecF64_mulAddZ31.scala 773:42]
  wire [53:0] sigAdjT_E = _T_1268[53:0] + _GEN_91; // @[DivSqrtRecF64_mulAddZ31.scala 773:42]
  wire [52:0] _T_1272 = ~roundMask_E; // @[DivSqrtRecF64_mulAddZ31.scala 774:47]
  wire [53:0] _T_1273 = {1'h1,_T_1272}; // @[Cat.scala 30:58]
  wire [53:0] sigY0_E = sigAdjT_E & _T_1273; // @[DivSqrtRecF64_mulAddZ31.scala 774:29]
  wire [53:0] _T_1276 = sigAdjT_E | _T_1249; // @[DivSqrtRecF64_mulAddZ31.scala 775:30]
  wire [53:0] sigY1_E = _T_1276 + 54'h1; // @[DivSqrtRecF64_mulAddZ31.scala 775:62]
  wire  _T_1282 = ~isZeroRemT_E; // @[DivSqrtRecF64_mulAddZ31.scala 783:41]
  wire  trueLtX_E1 = sqrtOp_PC ? ~isNegRemT_E & ~isZeroRemT_E : isNegRemT_E; // @[DivSqrtRecF64_mulAddZ31.scala 783:12]
  wire  _T_1286 = ~trueLtX_E1; // @[DivSqrtRecF64_mulAddZ31.scala 793:32]
  wire  _T_1289 = roundMask_E[0] & ~trueLtX_E1 & all1sHiRoundExtraT_E & extraT_E; // @[DivSqrtRecF64_mulAddZ31.scala 793:69]
  wire  hiRoundPosBit_E1 = hiRoundPosBitT_E ^ _T_1289; // @[DivSqrtRecF64_mulAddZ31.scala 792:26]
  wire  anyRoundExtra_E1 = _T_1282 | _T_1010 | ~all1sHiRoundExtraT_E; // @[DivSqrtRecF64_mulAddZ31.scala 795:55]
  wire  _T_1299 = ~anyRoundExtra_E1; // @[DivSqrtRecF64_mulAddZ31.scala 798:17]
  wire [53:0] roundEvenMask_E1 = roundingMode_near_even_PC & hiRoundPosBit_E1 & _T_1299 ? incrPosMask_E : 54'h0; // @[DivSqrtRecF64_mulAddZ31.scala 797:12]
  wire  _T_1309 = extraT_E & _T_1286; // @[DivSqrtRecF64_mulAddZ31.scala 806:29]
  wire  _T_1314 = ~all1sHiRoundT_E; // @[DivSqrtRecF64_mulAddZ31.scala 807:23]
  wire  _T_1315 = extraT_E & _T_1286 & _T_1282 | _T_1314; // @[DivSqrtRecF64_mulAddZ31.scala 806:62]
  wire  _T_1316 = roundMagUp_PC & _T_1315; // @[DivSqrtRecF64_mulAddZ31.scala 805:28]
  wire  _T_1317 = roundMagDown_PC & extraT_E & _T_1286 & all1sHiRoundT_E | _T_1316; // @[DivSqrtRecF64_mulAddZ31.scala 804:78]
  wire  _T_1324 = (extraT_E | _T_1286) & _T_1265; // @[DivSqrtRecF64_mulAddZ31.scala 810:51]
  wire  _T_1325 = hiRoundPosBitT_E | _T_1324; // @[DivSqrtRecF64_mulAddZ31.scala 809:36]
  wire  _T_1329 = _T_1309 & all1sHiRoundExtraT_E; // @[DivSqrtRecF64_mulAddZ31.scala 811:49]
  wire  _T_1330 = _T_1325 | _T_1329; // @[DivSqrtRecF64_mulAddZ31.scala 810:72]
  wire  _T_1331 = roundingMode_near_even_PC & _T_1330; // @[DivSqrtRecF64_mulAddZ31.scala 808:40]
  wire  _T_1332 = _T_1317 | _T_1331; // @[DivSqrtRecF64_mulAddZ31.scala 807:43]
  wire [53:0] _T_1333 = _T_1332 ? sigY1_E : sigY0_E; // @[DivSqrtRecF64_mulAddZ31.scala 804:12]
  wire [53:0] _T_1334 = ~roundEvenMask_E1; // @[DivSqrtRecF64_mulAddZ31.scala 814:13]
  wire [53:0] sigY_E1 = _T_1333 & _T_1334; // @[DivSqrtRecF64_mulAddZ31.scala 814:11]
  wire [51:0] fractY_E1 = sigY_E1[51:0]; // @[DivSqrtRecF64_mulAddZ31.scala 815:28]
  wire  inexactY_E1 = hiRoundPosBit_E1 | anyRoundExtra_E1; // @[DivSqrtRecF64_mulAddZ31.scala 816:40]
  wire [13:0] _T_1339 = ~sigY_E1[53] ? sExpX_E : 14'h0; // @[DivSqrtRecF64_mulAddZ31.scala 818:12]
  wire  _T_1343 = sigY_E1[53] & _T_390; // @[DivSqrtRecF64_mulAddZ31.scala 819:25]
  wire [13:0] _T_1346 = sigY_E1[53] & _T_390 & E_E_div ? expP1_PC : 14'h0; // @[DivSqrtRecF64_mulAddZ31.scala 819:12]
  wire [13:0] _T_1347 = _T_1339 | _T_1346; // @[DivSqrtRecF64_mulAddZ31.scala 818:73]
  wire [13:0] _T_1356 = _T_1343 & _T_993 ? expP2_PC : 14'h0; // @[DivSqrtRecF64_mulAddZ31.scala 820:12]
  wire [13:0] _T_1357 = _T_1347 | _T_1356; // @[DivSqrtRecF64_mulAddZ31.scala 819:73]
  wire [12:0] _T_1363 = expP2_PC[13:1] + 13'h400; // @[DivSqrtRecF64_mulAddZ31.scala 822:27]
  wire [12:0] _T_1365 = sigY_E1[53] & sqrtOp_PC ? _T_1363 : 13'h0; // @[DivSqrtRecF64_mulAddZ31.scala 821:12]
  wire [13:0] _GEN_92 = {{1'd0}, _T_1365}; // @[DivSqrtRecF64_mulAddZ31.scala 820:73]
  wire [13:0] sExpY_E1 = _T_1357 | _GEN_92; // @[DivSqrtRecF64_mulAddZ31.scala 820:73]
  wire [11:0] expY_E1 = sExpY_E1[11:0]; // @[DivSqrtRecF64_mulAddZ31.scala 825:27]
  wire  overflowY_E1 = ~sExpY_E1[13] & 3'h3 <= sExpY_E1[12:10]; // @[DivSqrtRecF64_mulAddZ31.scala 827:39]
  wire  totalUnderflowY_E1 = sExpY_E1[13] | sExpY_E1[12:0] < 13'h3ce; // @[DivSqrtRecF64_mulAddZ31.scala 830:22]
  wire  _T_1378 = posExpX_E <= 13'h401 & inexactY_E1; // @[DivSqrtRecF64_mulAddZ31.scala 833:56]
  wire  underflowY_E1 = totalUnderflowY_E1 | _T_1378; // @[DivSqrtRecF64_mulAddZ31.scala 832:28]
  wire  _T_1384 = ~isNaNB_PC & _T_354 & sign_PC; // @[DivSqrtRecF64_mulAddZ31.scala 839:41]
  wire  _T_1387 = isZeroA_PC & isZeroB_PC | isInfA_PC & isInfB_PC; // @[DivSqrtRecF64_mulAddZ31.scala 840:40]
  wire  notSigNaN_invalid_PC = sqrtOp_PC ? _T_1384 : _T_1387; // @[DivSqrtRecF64_mulAddZ31.scala 838:12]
  wire  invalid_PC = _T_390 & isSigNaNA_PC | isSigNaNB_PC | notSigNaN_invalid_PC; // @[DivSqrtRecF64_mulAddZ31.scala 843:55]
  wire  infinity_PC = _T_390 & _T_360 & _T_365 & isZeroB_PC; // @[DivSqrtRecF64_mulAddZ31.scala 845:56]
  wire  overflow_E1 = normalCase_PC & overflowY_E1; // @[DivSqrtRecF64_mulAddZ31.scala 847:37]
  wire  underflow_E1 = normalCase_PC & underflowY_E1; // @[DivSqrtRecF64_mulAddZ31.scala 848:38]
  wire  inexact_E1 = overflow_E1 | underflow_E1 | normalCase_PC & inexactY_E1; // @[DivSqrtRecF64_mulAddZ31.scala 852:37]
  wire  _T_1406 = isZeroA_PC | isInfB_PC | totalUnderflowY_E1 & _T_380; // @[DivSqrtRecF64_mulAddZ31.scala 857:37]
  wire  notSpecial_isZeroOut_E1 = sqrtOp_PC ? isZeroB_PC : _T_1406; // @[DivSqrtRecF64_mulAddZ31.scala 855:12]
  wire  pegMinFiniteMagOut_E1 = normalCase_PC & totalUnderflowY_E1 & roundMagUp_PC; // @[DivSqrtRecF64_mulAddZ31.scala 860:45]
  wire  pegMaxFiniteMagOut_E1 = overflow_E1 & ~overflowY_roundMagUp_PC; // @[DivSqrtRecF64_mulAddZ31.scala 861:45]
  wire  _T_1412 = isInfA_PC | isZeroB_PC | overflow_E1 & overflowY_roundMagUp_PC; // @[DivSqrtRecF64_mulAddZ31.scala 865:37]
  wire  notNaN_isInfOut_E1 = sqrtOp_PC ? isInfB_PC : _T_1412; // @[DivSqrtRecF64_mulAddZ31.scala 863:12]
  wire  isNaNOut_PC = _T_390 & isNaNA_PC | isNaNB_PC | notSigNaN_invalid_PC; // @[DivSqrtRecF64_mulAddZ31.scala 868:49]
  wire  _T_1420 = sqrtOp_PC ? isZeroB_PC & sign_PC : sign_PC; // @[DivSqrtRecF64_mulAddZ31.scala 871:29]
  wire  signOut_PC = ~isNaNOut_PC & _T_1420; // @[DivSqrtRecF64_mulAddZ31.scala 871:23]
  wire [11:0] _T_1424 = notSpecial_isZeroOut_E1 ? 12'he00 : 12'h0; // @[DivSqrtRecF64_mulAddZ31.scala 874:18]
  wire [11:0] _T_1425 = ~_T_1424; // @[DivSqrtRecF64_mulAddZ31.scala 874:14]
  wire [11:0] _T_1426 = expY_E1 & _T_1425; // @[DivSqrtRecF64_mulAddZ31.scala 873:18]
  wire [11:0] _T_1430 = pegMinFiniteMagOut_E1 ? 12'hc31 : 12'h0; // @[DivSqrtRecF64_mulAddZ31.scala 878:18]
  wire [11:0] _T_1431 = ~_T_1430; // @[DivSqrtRecF64_mulAddZ31.scala 878:14]
  wire [11:0] _T_1432 = _T_1426 & _T_1431; // @[DivSqrtRecF64_mulAddZ31.scala 877:16]
  wire [11:0] _T_1436 = pegMaxFiniteMagOut_E1 ? 12'h400 : 12'h0; // @[DivSqrtRecF64_mulAddZ31.scala 882:18]
  wire [11:0] _T_1437 = ~_T_1436; // @[DivSqrtRecF64_mulAddZ31.scala 882:14]
  wire [11:0] _T_1438 = _T_1432 & _T_1437; // @[DivSqrtRecF64_mulAddZ31.scala 881:16]
  wire [11:0] _T_1442 = notNaN_isInfOut_E1 ? 12'h200 : 12'h0; // @[DivSqrtRecF64_mulAddZ31.scala 886:18]
  wire [11:0] _T_1443 = ~_T_1442; // @[DivSqrtRecF64_mulAddZ31.scala 886:14]
  wire [11:0] _T_1444 = _T_1438 & _T_1443; // @[DivSqrtRecF64_mulAddZ31.scala 885:16]
  wire [11:0] _T_1447 = pegMinFiniteMagOut_E1 ? 12'h3ce : 12'h0; // @[DivSqrtRecF64_mulAddZ31.scala 890:16]
  wire [11:0] _T_1448 = _T_1444 | _T_1447; // @[DivSqrtRecF64_mulAddZ31.scala 889:17]
  wire [11:0] _T_1451 = pegMaxFiniteMagOut_E1 ? 12'hbff : 12'h0; // @[DivSqrtRecF64_mulAddZ31.scala 891:16]
  wire [11:0] _T_1452 = _T_1448 | _T_1451; // @[DivSqrtRecF64_mulAddZ31.scala 890:76]
  wire [11:0] _T_1455 = notNaN_isInfOut_E1 ? 12'hc00 : 12'h0; // @[DivSqrtRecF64_mulAddZ31.scala 892:16]
  wire [11:0] _T_1456 = _T_1452 | _T_1455; // @[DivSqrtRecF64_mulAddZ31.scala 891:76]
  wire [11:0] _T_1459 = isNaNOut_PC ? 12'he00 : 12'h0; // @[DivSqrtRecF64_mulAddZ31.scala 893:16]
  wire [11:0] expOut_E1 = _T_1456 | _T_1459; // @[DivSqrtRecF64_mulAddZ31.scala 892:76]
  wire [51:0] _T_1465 = isNaNOut_PC ? 52'h8000000000000 : 52'h0; // @[DivSqrtRecF64_mulAddZ31.scala 896:16]
  wire [51:0] _T_1466 = notSpecial_isZeroOut_E1 | totalUnderflowY_E1 | isNaNOut_PC ? _T_1465 : fractY_E1; // @[DivSqrtRecF64_mulAddZ31.scala 895:12]
  wire [51:0] _T_1470 = pegMaxFiniteMagOut_E1 ? 52'hfffffffffffff : 52'h0; // @[Bitwise.scala 71:12]
  wire [51:0] fractOut_E1 = _T_1466 | _T_1470; // @[DivSqrtRecF64_mulAddZ31.scala 898:11]
  wire [12:0] _T_1471 = {signOut_PC,expOut_E1}; // @[Cat.scala 30:58]
  wire [1:0] _T_1473 = {underflow_E1,inexact_E1}; // @[Cat.scala 30:58]
  wire [2:0] _T_1475 = {invalid_PC,infinity_PC,overflow_E1}; // @[Cat.scala 30:58]
  assign io_inReady_div = _T_155 & ~cyc_C4; // @[DivSqrtRecF64_mulAddZ31.scala 199:22]
  assign io_inReady_sqrt = _T_170 & _T_151; // @[DivSqrtRecF64_mulAddZ31.scala 202:26]
  assign io_outValid_div = leaving_PC & ~sqrtOp_PC; // @[DivSqrtRecF64_mulAddZ31.scala 383:36]
  assign io_outValid_sqrt = leaving_PC & sqrtOp_PC; // @[DivSqrtRecF64_mulAddZ31.scala 384:36]
  assign io_out = {_T_1471,fractOut_E1}; // @[Cat.scala 30:58]
  assign io_exceptionFlags = {_T_1475,_T_1473}; // @[Cat.scala 30:58]
  assign io_usingMulAdd = {_T_978,_T_977}; // @[Cat.scala 30:58]
  assign io_latchMulAddA_0 = _T_886 | cyc_C4 | cyc_C1; // @[DivSqrtRecF64_mulAddZ31.scala 648:35]
  assign io_mulAddA_0 = _GEN_71 | zComplSigT_C1_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 658:67]
  assign io_latchMulAddB_0 = _T_922 | cyc_C4 | cyc_C1; // @[DivSqrtRecF64_mulAddZ31.scala 662:35]
  assign io_mulAddB_0 = _GEN_77 | zComplSigT_C1; // @[DivSqrtRecF64_mulAddZ31.scala 669:55]
  assign io_mulAddC_2 = _T_998 | _GEN_80; // @[DivSqrtRecF64_mulAddZ31.scala 691:64]
  always @(posedge clock) begin
    if (reset) begin // @[DivSqrtRecF64_mulAddZ31.scala 78:30]
      valid_PA <= 1'h0; // @[DivSqrtRecF64_mulAddZ31.scala 78:30]
    end else if (entering_PA | leaving_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 240:38]
      valid_PA <= entering_PA; // @[DivSqrtRecF64_mulAddZ31.scala 241:18]
    end
    if (entering_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 243:24]
      sqrtOp_PA <= io_sqrtOp; // @[DivSqrtRecF64_mulAddZ31.scala 244:25]
    end
    if (entering_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 243:24]
      if (io_sqrtOp) begin // @[DivSqrtRecF64_mulAddZ31.scala 221:21]
        sign_PA <= signB_S;
      end else begin
        sign_PA <= signA_S ^ signB_S;
      end
    end
    if (entering_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 243:24]
      specialCodeB_PA <= specialCodeB_S; // @[DivSqrtRecF64_mulAddZ31.scala 246:25]
    end
    if (entering_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 243:24]
      fractB_51_PA <= fractB_S[51]; // @[DivSqrtRecF64_mulAddZ31.scala 247:25]
    end
    if (entering_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 243:24]
      roundingMode_PA <= io_roundingMode; // @[DivSqrtRecF64_mulAddZ31.scala 248:25]
    end
    if (entering_PA & _T_176) begin // @[DivSqrtRecF64_mulAddZ31.scala 250:39]
      specialCodeA_PA <= specialCodeA_S; // @[DivSqrtRecF64_mulAddZ31.scala 251:25]
    end
    if (entering_PA & _T_176) begin // @[DivSqrtRecF64_mulAddZ31.scala 250:39]
      fractA_51_PA <= fractA_S[51]; // @[DivSqrtRecF64_mulAddZ31.scala 252:25]
    end
    if (entering_PA_normalCase) begin // @[DivSqrtRecF64_mulAddZ31.scala 254:35]
      if (io_sqrtOp) begin // @[DivSqrtRecF64_mulAddZ31.scala 256:16]
        exp_PA <= {{2'd0}, expB_S};
      end else begin
        exp_PA <= _T_242;
      end
    end
    if (entering_PA_normalCase) begin // @[DivSqrtRecF64_mulAddZ31.scala 254:35]
      fractB_other_PA <= fractB_S[50:0]; // @[DivSqrtRecF64_mulAddZ31.scala 260:25]
    end
    if (cyc_A4_div) begin // @[DivSqrtRecF64_mulAddZ31.scala 262:39]
      fractA_other_PA <= fractA_S[50:0]; // @[DivSqrtRecF64_mulAddZ31.scala 263:25]
    end
    if (reset) begin // @[DivSqrtRecF64_mulAddZ31.scala 91:30]
      valid_PB <= 1'h0; // @[DivSqrtRecF64_mulAddZ31.scala 91:30]
    end else if (entering_PB | leaving_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 290:38]
      valid_PB <= entering_PB; // @[DivSqrtRecF64_mulAddZ31.scala 291:18]
    end
    if (entering_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 293:24]
      if (valid_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 294:31]
        sqrtOp_PB <= sqrtOp_PA;
      end else begin
        sqrtOp_PB <= io_sqrtOp;
      end
    end
    if (entering_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 293:24]
      if (valid_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 295:31]
        sign_PB <= sign_PA;
      end else if (io_sqrtOp) begin // @[DivSqrtRecF64_mulAddZ31.scala 221:21]
        sign_PB <= signB_S;
      end else begin
        sign_PB <= signA_S ^ signB_S;
      end
    end
    if (entering_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 293:24]
      if (valid_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 296:31]
        specialCodeA_PB <= specialCodeA_PA;
      end else begin
        specialCodeA_PB <= specialCodeA_S;
      end
    end
    if (entering_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 293:24]
      if (valid_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 297:31]
        fractA_51_PB <= fractA_51_PA;
      end else begin
        fractA_51_PB <= fractA_S[51];
      end
    end
    if (entering_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 293:24]
      if (valid_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 298:31]
        specialCodeB_PB <= specialCodeB_PA;
      end else begin
        specialCodeB_PB <= specialCodeB_S;
      end
    end
    if (entering_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 293:24]
      if (valid_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 299:31]
        fractB_51_PB <= fractB_51_PA;
      end else begin
        fractB_51_PB <= fractB_S[51];
      end
    end
    if (entering_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 293:24]
      if (valid_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 300:31]
        roundingMode_PB <= roundingMode_PA;
      end else begin
        roundingMode_PB <= io_roundingMode;
      end
    end
    if (entering_PB_normalCase) begin // @[DivSqrtRecF64_mulAddZ31.scala 302:35]
      exp_PB <= exp_PA; // @[DivSqrtRecF64_mulAddZ31.scala 303:25]
    end
    if (entering_PB_normalCase) begin // @[DivSqrtRecF64_mulAddZ31.scala 302:35]
      fractA_0_PB <= fractA_other_PA[0]; // @[DivSqrtRecF64_mulAddZ31.scala 304:25]
    end
    if (entering_PB_normalCase) begin // @[DivSqrtRecF64_mulAddZ31.scala 302:35]
      fractB_other_PB <= fractB_other_PA; // @[DivSqrtRecF64_mulAddZ31.scala 305:25]
    end
    if (reset) begin // @[DivSqrtRecF64_mulAddZ31.scala 104:30]
      valid_PC <= 1'h0; // @[DivSqrtRecF64_mulAddZ31.scala 104:30]
    end else if (entering_PC | leaving_PC) begin // @[DivSqrtRecF64_mulAddZ31.scala 328:38]
      valid_PC <= entering_PC; // @[DivSqrtRecF64_mulAddZ31.scala 329:18]
    end
    if (entering_PC) begin // @[DivSqrtRecF64_mulAddZ31.scala 331:24]
      if (valid_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 332:31]
        sqrtOp_PC <= sqrtOp_PB;
      end else begin
        sqrtOp_PC <= io_sqrtOp;
      end
    end
    if (entering_PC) begin // @[DivSqrtRecF64_mulAddZ31.scala 331:24]
      if (valid_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 333:31]
        sign_PC <= sign_PB;
      end else if (io_sqrtOp) begin // @[DivSqrtRecF64_mulAddZ31.scala 221:21]
        sign_PC <= signB_S;
      end else begin
        sign_PC <= signA_S ^ signB_S;
      end
    end
    if (entering_PC) begin // @[DivSqrtRecF64_mulAddZ31.scala 331:24]
      if (valid_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 334:31]
        specialCodeA_PC <= specialCodeA_PB;
      end else begin
        specialCodeA_PC <= specialCodeA_S;
      end
    end
    if (entering_PC) begin // @[DivSqrtRecF64_mulAddZ31.scala 331:24]
      if (valid_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 335:31]
        fractA_51_PC <= fractA_51_PB;
      end else begin
        fractA_51_PC <= fractA_S[51];
      end
    end
    if (entering_PC) begin // @[DivSqrtRecF64_mulAddZ31.scala 331:24]
      if (valid_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 336:31]
        specialCodeB_PC <= specialCodeB_PB;
      end else begin
        specialCodeB_PC <= specialCodeB_S;
      end
    end
    if (entering_PC) begin // @[DivSqrtRecF64_mulAddZ31.scala 331:24]
      if (valid_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 337:31]
        fractB_51_PC <= fractB_51_PB;
      end else begin
        fractB_51_PC <= fractB_S[51];
      end
    end
    if (entering_PC) begin // @[DivSqrtRecF64_mulAddZ31.scala 331:24]
      if (valid_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 338:31]
        roundingMode_PC <= roundingMode_PB;
      end else begin
        roundingMode_PC <= io_roundingMode;
      end
    end
    if (entering_PC_normalCase) begin // @[DivSqrtRecF64_mulAddZ31.scala 340:35]
      exp_PC <= exp_PB; // @[DivSqrtRecF64_mulAddZ31.scala 341:25]
    end
    if (entering_PC_normalCase) begin // @[DivSqrtRecF64_mulAddZ31.scala 340:35]
      fractA_0_PC <= fractA_0_PB; // @[DivSqrtRecF64_mulAddZ31.scala 342:25]
    end
    if (entering_PC_normalCase) begin // @[DivSqrtRecF64_mulAddZ31.scala 340:35]
      fractB_other_PC <= fractB_other_PB; // @[DivSqrtRecF64_mulAddZ31.scala 343:25]
    end
    if (reset) begin // @[DivSqrtRecF64_mulAddZ31.scala 117:30]
      cycleNum_A <= 3'h0; // @[DivSqrtRecF64_mulAddZ31.scala 117:30]
    end else if (entering_PA_normalCase | cycleNum_A != 3'h0) begin // @[DivSqrtRecF64_mulAddZ31.scala 388:63]
      cycleNum_A <= _T_411; // @[DivSqrtRecF64_mulAddZ31.scala 389:20]
    end
    if (reset) begin // @[DivSqrtRecF64_mulAddZ31.scala 118:30]
      cycleNum_B <= 4'h0; // @[DivSqrtRecF64_mulAddZ31.scala 118:30]
    end else if (cyc_A1 | cycleNum_B != 4'h0) begin // @[DivSqrtRecF64_mulAddZ31.scala 415:47]
      if (cyc_A1) begin // @[DivSqrtRecF64_mulAddZ31.scala 417:16]
        if (sqrtOp_PA) begin // @[DivSqrtRecF64_mulAddZ31.scala 418:20]
          cycleNum_B <= 4'ha;
        end else begin
          cycleNum_B <= 4'h6;
        end
      end else begin
        cycleNum_B <= _T_433;
      end
    end
    if (reset) begin // @[DivSqrtRecF64_mulAddZ31.scala 119:30]
      cycleNum_C <= 3'h0; // @[DivSqrtRecF64_mulAddZ31.scala 119:30]
    end else if (cyc_B1 | cycleNum_C != 3'h0) begin // @[DivSqrtRecF64_mulAddZ31.scala 449:47]
      if (cyc_B1) begin // @[DivSqrtRecF64_mulAddZ31.scala 451:16]
        if (sqrtOp_PB) begin // @[DivSqrtRecF64_mulAddZ31.scala 451:28]
          cycleNum_C <= 3'h6;
        end else begin
          cycleNum_C <= 3'h5;
        end
      end else begin
        cycleNum_C <= _T_494;
      end
    end
    if (reset) begin // @[DivSqrtRecF64_mulAddZ31.scala 120:30]
      cycleNum_E <= 3'h0; // @[DivSqrtRecF64_mulAddZ31.scala 120:30]
    end else if (cyc_C1 | cycleNum_E != 3'h0) begin // @[DivSqrtRecF64_mulAddZ31.scala 474:47]
      if (cyc_C1) begin // @[DivSqrtRecF64_mulAddZ31.scala 475:26]
        cycleNum_E <= 3'h4;
      end else begin
        cycleNum_E <= _T_524;
      end
    end
    if (cyc_A6_sqrt | cyc_A4_div) begin // @[DivSqrtRecF64_mulAddZ31.scala 605:38]
      fractR0_A <= _T_833; // @[DivSqrtRecF64_mulAddZ31.scala 606:19]
    end
    hiSqrR0_A_sqrt <= _GEN_38[9:0];
    if (cyc_A4_sqrt | cyc_A3) begin // @[DivSqrtRecF64_mulAddZ31.scala 613:34]
      partNegSigma0_A <= _T_837[20:0]; // @[DivSqrtRecF64_mulAddZ31.scala 615:25]
    end
    nextMulAdd9A_A <= _GEN_40[8:0];
    if (_T_841 | cyc_A2) begin // @[DivSqrtRecF64_mulAddZ31.scala 630:74]
      nextMulAdd9B_A <= _T_881; // @[DivSqrtRecF64_mulAddZ31.scala 631:24]
    end
    if (cyc_A1_sqrt) begin // @[DivSqrtRecF64_mulAddZ31.scala 640:24]
      if (exp_PA[0]) begin // @[DivSqrtRecF64_mulAddZ31.scala 603:26]
        ER1_B_sqrt <= _T_831;
      end else begin
        ER1_B_sqrt <= {{1'd0}, r1_A1};
      end
    end
    if (cyc_B8_sqrt) begin // @[DivSqrtRecF64_mulAddZ31.scala 723:24]
      ESqrR1_B_sqrt <= ESqrR1_B8_sqrt; // @[DivSqrtRecF64_mulAddZ31.scala 724:23]
    end
    if (cyc_B3) begin // @[DivSqrtRecF64_mulAddZ31.scala 726:19]
      sigX1_B <= sigXNU_B3_CX; // @[DivSqrtRecF64_mulAddZ31.scala 727:17]
    end
    if (cyc_B1) begin // @[DivSqrtRecF64_mulAddZ31.scala 729:19]
      sqrSigma1_C <= sqrSigma1_B1; // @[DivSqrtRecF64_mulAddZ31.scala 730:21]
    end
    if (cyc_C6_sqrt | cyc_C5_div | cyc_C3_sqrt) begin // @[DivSqrtRecF64_mulAddZ31.scala 733:53]
      sigXN_C <= sigXNU_B3_CX; // @[DivSqrtRecF64_mulAddZ31.scala 734:17]
    end
    if (cyc_C5_sqrt) begin // @[DivSqrtRecF64_mulAddZ31.scala 736:24]
      u_C_sqrt <= sigXNU_B3_CX[56:26]; // @[DivSqrtRecF64_mulAddZ31.scala 737:18]
    end
    if (cyc_C1) begin // @[DivSqrtRecF64_mulAddZ31.scala 739:19]
      E_E_div <= E_C1_div; // @[DivSqrtRecF64_mulAddZ31.scala 740:18]
    end
    if (cyc_C1) begin // @[DivSqrtRecF64_mulAddZ31.scala 739:19]
      sigT_E <= sigT_C1[53:1]; // @[DivSqrtRecF64_mulAddZ31.scala 741:18]
    end
    if (cyc_C1) begin // @[DivSqrtRecF64_mulAddZ31.scala 739:19]
      extraT_E <= sigT_C1[0]; // @[DivSqrtRecF64_mulAddZ31.scala 742:18]
    end
    if (cyc_E2) begin // @[DivSqrtRecF64_mulAddZ31.scala 745:19]
      if (sqrtOp_PC) begin // @[DivSqrtRecF64_mulAddZ31.scala 746:27]
        isNegRemT_E <= remT_E2[55];
      end else begin
        isNegRemT_E <= remT_E2[53];
      end
    end
    if (cyc_E2) begin // @[DivSqrtRecF64_mulAddZ31.scala 745:19]
      isZeroRemT_E <= _T_1062; // @[DivSqrtRecF64_mulAddZ31.scala 747:22]
    end
  end
// Register and memory initialization
`ifdef RANDOMIZE_GARBAGE_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_INVALID_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_REG_INIT
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_MEM_INIT
`define RANDOMIZE
`endif
`ifndef RANDOM
`define RANDOM $random
`endif
`ifdef RANDOMIZE_MEM_INIT
  integer initvar;
`endif
`ifndef SYNTHESIS
`ifdef FIRRTL_BEFORE_INITIAL
`FIRRTL_BEFORE_INITIAL
`endif
initial begin
  `ifdef RANDOMIZE
    `ifdef INIT_RANDOM
      `INIT_RANDOM
    `endif
    `ifndef VERILATOR
      `ifdef RANDOMIZE_DELAY
        #`RANDOMIZE_DELAY begin end
      `else
        #0.002 begin end
      `endif
    `endif
`ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  valid_PA = _RAND_0[0:0];
  _RAND_1 = {1{`RANDOM}};
  sqrtOp_PA = _RAND_1[0:0];
  _RAND_2 = {1{`RANDOM}};
  sign_PA = _RAND_2[0:0];
  _RAND_3 = {1{`RANDOM}};
  specialCodeB_PA = _RAND_3[2:0];
  _RAND_4 = {1{`RANDOM}};
  fractB_51_PA = _RAND_4[0:0];
  _RAND_5 = {1{`RANDOM}};
  roundingMode_PA = _RAND_5[1:0];
  _RAND_6 = {1{`RANDOM}};
  specialCodeA_PA = _RAND_6[2:0];
  _RAND_7 = {1{`RANDOM}};
  fractA_51_PA = _RAND_7[0:0];
  _RAND_8 = {1{`RANDOM}};
  exp_PA = _RAND_8[13:0];
  _RAND_9 = {2{`RANDOM}};
  fractB_other_PA = _RAND_9[50:0];
  _RAND_10 = {2{`RANDOM}};
  fractA_other_PA = _RAND_10[50:0];
  _RAND_11 = {1{`RANDOM}};
  valid_PB = _RAND_11[0:0];
  _RAND_12 = {1{`RANDOM}};
  sqrtOp_PB = _RAND_12[0:0];
  _RAND_13 = {1{`RANDOM}};
  sign_PB = _RAND_13[0:0];
  _RAND_14 = {1{`RANDOM}};
  specialCodeA_PB = _RAND_14[2:0];
  _RAND_15 = {1{`RANDOM}};
  fractA_51_PB = _RAND_15[0:0];
  _RAND_16 = {1{`RANDOM}};
  specialCodeB_PB = _RAND_16[2:0];
  _RAND_17 = {1{`RANDOM}};
  fractB_51_PB = _RAND_17[0:0];
  _RAND_18 = {1{`RANDOM}};
  roundingMode_PB = _RAND_18[1:0];
  _RAND_19 = {1{`RANDOM}};
  exp_PB = _RAND_19[13:0];
  _RAND_20 = {1{`RANDOM}};
  fractA_0_PB = _RAND_20[0:0];
  _RAND_21 = {2{`RANDOM}};
  fractB_other_PB = _RAND_21[50:0];
  _RAND_22 = {1{`RANDOM}};
  valid_PC = _RAND_22[0:0];
  _RAND_23 = {1{`RANDOM}};
  sqrtOp_PC = _RAND_23[0:0];
  _RAND_24 = {1{`RANDOM}};
  sign_PC = _RAND_24[0:0];
  _RAND_25 = {1{`RANDOM}};
  specialCodeA_PC = _RAND_25[2:0];
  _RAND_26 = {1{`RANDOM}};
  fractA_51_PC = _RAND_26[0:0];
  _RAND_27 = {1{`RANDOM}};
  specialCodeB_PC = _RAND_27[2:0];
  _RAND_28 = {1{`RANDOM}};
  fractB_51_PC = _RAND_28[0:0];
  _RAND_29 = {1{`RANDOM}};
  roundingMode_PC = _RAND_29[1:0];
  _RAND_30 = {1{`RANDOM}};
  exp_PC = _RAND_30[13:0];
  _RAND_31 = {1{`RANDOM}};
  fractA_0_PC = _RAND_31[0:0];
  _RAND_32 = {2{`RANDOM}};
  fractB_other_PC = _RAND_32[50:0];
  _RAND_33 = {1{`RANDOM}};
  cycleNum_A = _RAND_33[2:0];
  _RAND_34 = {1{`RANDOM}};
  cycleNum_B = _RAND_34[3:0];
  _RAND_35 = {1{`RANDOM}};
  cycleNum_C = _RAND_35[2:0];
  _RAND_36 = {1{`RANDOM}};
  cycleNum_E = _RAND_36[2:0];
  _RAND_37 = {1{`RANDOM}};
  fractR0_A = _RAND_37[8:0];
  _RAND_38 = {1{`RANDOM}};
  hiSqrR0_A_sqrt = _RAND_38[9:0];
  _RAND_39 = {1{`RANDOM}};
  partNegSigma0_A = _RAND_39[20:0];
  _RAND_40 = {1{`RANDOM}};
  nextMulAdd9A_A = _RAND_40[8:0];
  _RAND_41 = {1{`RANDOM}};
  nextMulAdd9B_A = _RAND_41[8:0];
  _RAND_42 = {1{`RANDOM}};
  ER1_B_sqrt = _RAND_42[16:0];
  _RAND_43 = {1{`RANDOM}};
  ESqrR1_B_sqrt = _RAND_43[31:0];
  _RAND_44 = {2{`RANDOM}};
  sigX1_B = _RAND_44[57:0];
  _RAND_45 = {2{`RANDOM}};
  sqrSigma1_C = _RAND_45[32:0];
  _RAND_46 = {2{`RANDOM}};
  sigXN_C = _RAND_46[57:0];
  _RAND_47 = {1{`RANDOM}};
  u_C_sqrt = _RAND_47[30:0];
  _RAND_48 = {1{`RANDOM}};
  E_E_div = _RAND_48[0:0];
  _RAND_49 = {2{`RANDOM}};
  sigT_E = _RAND_49[52:0];
  _RAND_50 = {1{`RANDOM}};
  extraT_E = _RAND_50[0:0];
  _RAND_51 = {1{`RANDOM}};
  isNegRemT_E = _RAND_51[0:0];
  _RAND_52 = {1{`RANDOM}};
  isZeroRemT_E = _RAND_52[0:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module Mul54(
  input          clock,
  input          io_val_s0,
  input          io_latch_a_s0,
  input  [53:0]  io_a_s0,
  input          io_latch_b_s0,
  input  [53:0]  io_b_s0,
  input  [104:0] io_c_s2,
  output [104:0] io_result_s3
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [63:0] _RAND_2;
  reg [63:0] _RAND_3;
  reg [63:0] _RAND_4;
  reg [63:0] _RAND_5;
  reg [127:0] _RAND_6;
`endif // RANDOMIZE_REG_INIT
  reg  val_s1; // @[DivSqrtRecF64.scala 96:21]
  reg  val_s2; // @[DivSqrtRecF64.scala 97:21]
  reg [53:0] reg_a_s1; // @[DivSqrtRecF64.scala 98:23]
  reg [53:0] reg_b_s1; // @[DivSqrtRecF64.scala 99:23]
  reg [53:0] reg_a_s2; // @[DivSqrtRecF64.scala 100:23]
  reg [53:0] reg_b_s2; // @[DivSqrtRecF64.scala 101:23]
  reg [104:0] reg_result_s3; // @[DivSqrtRecF64.scala 102:28]
  wire [107:0] _T_23 = reg_a_s2 * reg_b_s2; // @[DivSqrtRecF64.scala 122:36]
  wire [104:0] _T_26 = _T_23[104:0] + io_c_s2; // @[DivSqrtRecF64.scala 122:55]
  assign io_result_s3 = reg_result_s3; // @[DivSqrtRecF64.scala 125:18]
  always @(posedge clock) begin
    val_s1 <= io_val_s0; // @[DivSqrtRecF64.scala 104:12]
    val_s2 <= val_s1; // @[DivSqrtRecF64.scala 105:12]
    if (io_val_s0) begin // @[DivSqrtRecF64.scala 107:22]
      if (io_latch_a_s0) begin // @[DivSqrtRecF64.scala 108:30]
        reg_a_s1 <= io_a_s0; // @[DivSqrtRecF64.scala 109:22]
      end
    end
    if (io_val_s0) begin // @[DivSqrtRecF64.scala 107:22]
      if (io_latch_b_s0) begin // @[DivSqrtRecF64.scala 111:30]
        reg_b_s1 <= io_b_s0; // @[DivSqrtRecF64.scala 112:22]
      end
    end
    if (val_s1) begin // @[DivSqrtRecF64.scala 116:19]
      reg_a_s2 <= reg_a_s1; // @[DivSqrtRecF64.scala 117:18]
    end
    if (val_s1) begin // @[DivSqrtRecF64.scala 116:19]
      reg_b_s2 <= reg_b_s1; // @[DivSqrtRecF64.scala 118:18]
    end
    if (val_s2) begin // @[DivSqrtRecF64.scala 121:19]
      reg_result_s3 <= _T_26; // @[DivSqrtRecF64.scala 122:23]
    end
  end
// Register and memory initialization
`ifdef RANDOMIZE_GARBAGE_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_INVALID_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_REG_INIT
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_MEM_INIT
`define RANDOMIZE
`endif
`ifndef RANDOM
`define RANDOM $random
`endif
`ifdef RANDOMIZE_MEM_INIT
  integer initvar;
`endif
`ifndef SYNTHESIS
`ifdef FIRRTL_BEFORE_INITIAL
`FIRRTL_BEFORE_INITIAL
`endif
initial begin
  `ifdef RANDOMIZE
    `ifdef INIT_RANDOM
      `INIT_RANDOM
    `endif
    `ifndef VERILATOR
      `ifdef RANDOMIZE_DELAY
        #`RANDOMIZE_DELAY begin end
      `else
        #0.002 begin end
      `endif
    `endif
`ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  val_s1 = _RAND_0[0:0];
  _RAND_1 = {1{`RANDOM}};
  val_s2 = _RAND_1[0:0];
  _RAND_2 = {2{`RANDOM}};
  reg_a_s1 = _RAND_2[53:0];
  _RAND_3 = {2{`RANDOM}};
  reg_b_s1 = _RAND_3[53:0];
  _RAND_4 = {2{`RANDOM}};
  reg_a_s2 = _RAND_4[53:0];
  _RAND_5 = {2{`RANDOM}};
  reg_b_s2 = _RAND_5[53:0];
  _RAND_6 = {4{`RANDOM}};
  reg_result_s3 = _RAND_6[104:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module RoundRawFNToRecFN_1(
  input         io_invalidExc,
  input         io_in_sign,
  input         io_in_isNaN,
  input         io_in_isInf,
  input         io_in_isZero,
  input  [9:0]  io_in_sExp,
  input  [26:0] io_in_sig,
  input  [1:0]  io_roundingMode,
  output [32:0] io_out,
  output [4:0]  io_exceptionFlags
);
  wire  roundingMode_nearest_even = io_roundingMode == 2'h0; // @[RoundRawFNToRecFN.scala 88:54]
  wire  roundingMode_min = io_roundingMode == 2'h2; // @[RoundRawFNToRecFN.scala 90:54]
  wire  roundingMode_max = io_roundingMode == 2'h3; // @[RoundRawFNToRecFN.scala 91:54]
  wire  roundMagUp = roundingMode_min & io_in_sign | roundingMode_max & ~io_in_sign; // @[RoundRawFNToRecFN.scala 94:42]
  wire  doShiftSigDown1 = io_in_sig[26]; // @[RoundRawFNToRecFN.scala 98:36]
  wire  isNegExp = $signed(io_in_sExp) < 10'sh0; // @[RoundRawFNToRecFN.scala 99:32]
  wire [24:0] _T_34 = isNegExp ? 25'h1ffffff : 25'h0; // @[Bitwise.scala 71:12]
  wire [8:0] _T_36 = ~io_in_sExp[8:0]; // @[primitives.scala 50:21]
  wire [64:0] _T_45 = 65'sh10000000000000000 >>> _T_36[5:0]; // @[primitives.scala 68:52]
  wire [15:0] _T_53 = {{8'd0}, _T_45[57:50]}; // @[Bitwise.scala 102:31]
  wire [15:0] _T_55 = {_T_45[49:42], 8'h0}; // @[Bitwise.scala 102:65]
  wire [15:0] _T_57 = _T_55 & 16'hff00; // @[Bitwise.scala 102:75]
  wire [15:0] _T_58 = _T_53 | _T_57; // @[Bitwise.scala 102:39]
  wire [15:0] _GEN_0 = {{4'd0}, _T_58[15:4]}; // @[Bitwise.scala 102:31]
  wire [15:0] _T_63 = _GEN_0 & 16'hf0f; // @[Bitwise.scala 102:31]
  wire [15:0] _T_65 = {_T_58[11:0], 4'h0}; // @[Bitwise.scala 102:65]
  wire [15:0] _T_67 = _T_65 & 16'hf0f0; // @[Bitwise.scala 102:75]
  wire [15:0] _T_68 = _T_63 | _T_67; // @[Bitwise.scala 102:39]
  wire [15:0] _GEN_1 = {{2'd0}, _T_68[15:2]}; // @[Bitwise.scala 102:31]
  wire [15:0] _T_73 = _GEN_1 & 16'h3333; // @[Bitwise.scala 102:31]
  wire [15:0] _T_75 = {_T_68[13:0], 2'h0}; // @[Bitwise.scala 102:65]
  wire [15:0] _T_77 = _T_75 & 16'hcccc; // @[Bitwise.scala 102:75]
  wire [15:0] _T_78 = _T_73 | _T_77; // @[Bitwise.scala 102:39]
  wire [15:0] _GEN_2 = {{1'd0}, _T_78[15:1]}; // @[Bitwise.scala 102:31]
  wire [15:0] _T_83 = _GEN_2 & 16'h5555; // @[Bitwise.scala 102:31]
  wire [15:0] _T_85 = {_T_78[14:0], 1'h0}; // @[Bitwise.scala 102:65]
  wire [15:0] _T_87 = _T_85 & 16'haaaa; // @[Bitwise.scala 102:75]
  wire [15:0] _T_88 = _T_83 | _T_87; // @[Bitwise.scala 102:39]
  wire [21:0] _T_105 = {_T_88,_T_45[58],_T_45[59],_T_45[60],_T_45[61],_T_45[62],_T_45[63]}; // @[Cat.scala 30:58]
  wire [21:0] _T_106 = ~_T_105; // @[primitives.scala 65:36]
  wire [21:0] _T_107 = _T_36[6] ? 22'h0 : _T_106; // @[primitives.scala 65:21]
  wire [21:0] _T_108 = ~_T_107; // @[primitives.scala 65:17]
  wire [24:0] _T_110 = {_T_108,3'h7}; // @[Cat.scala 30:58]
  wire [2:0] _T_121 = {_T_45[0],_T_45[1],_T_45[2]}; // @[Cat.scala 30:58]
  wire [2:0] _T_123 = _T_36[6] ? _T_121 : 3'h0; // @[primitives.scala 59:20]
  wire [24:0] _T_124 = _T_36[7] ? _T_110 : {{22'd0}, _T_123}; // @[primitives.scala 61:20]
  wire [24:0] _T_126 = _T_36[8] ? _T_124 : 25'h0; // @[primitives.scala 59:20]
  wire [24:0] _T_127 = _T_34 | _T_126; // @[RoundRawFNToRecFN.scala 101:42]
  wire [24:0] _GEN_3 = {{24'd0}, doShiftSigDown1}; // @[RoundRawFNToRecFN.scala 106:19]
  wire [24:0] _T_128 = _T_127 | _GEN_3; // @[RoundRawFNToRecFN.scala 106:19]
  wire [26:0] roundMask = {_T_128,2'h3}; // @[Cat.scala 30:58]
  wire [27:0] _T_130 = {isNegExp,_T_128,2'h3}; // @[Cat.scala 30:58]
  wire [26:0] shiftedRoundMask = _T_130[27:1]; // @[RoundRawFNToRecFN.scala 109:52]
  wire [26:0] _T_131 = ~shiftedRoundMask; // @[RoundRawFNToRecFN.scala 110:24]
  wire [26:0] roundPosMask = _T_131 & roundMask; // @[RoundRawFNToRecFN.scala 110:42]
  wire [26:0] _T_132 = io_in_sig & roundPosMask; // @[RoundRawFNToRecFN.scala 111:34]
  wire  roundPosBit = _T_132 != 27'h0; // @[RoundRawFNToRecFN.scala 111:50]
  wire [26:0] _T_134 = io_in_sig & shiftedRoundMask; // @[RoundRawFNToRecFN.scala 112:36]
  wire  anyRoundExtra = _T_134 != 27'h0; // @[RoundRawFNToRecFN.scala 112:56]
  wire  anyRound = roundPosBit | anyRoundExtra; // @[RoundRawFNToRecFN.scala 113:32]
  wire  _T_136 = roundingMode_nearest_even & roundPosBit; // @[RoundRawFNToRecFN.scala 116:40]
  wire  _T_137 = roundMagUp & anyRound; // @[RoundRawFNToRecFN.scala 117:29]
  wire [26:0] _T_139 = io_in_sig | roundMask; // @[RoundRawFNToRecFN.scala 118:26]
  wire [25:0] _T_142 = _T_139[26:2] + 25'h1; // @[RoundRawFNToRecFN.scala 118:43]
  wire  _T_145 = ~anyRoundExtra; // @[RoundRawFNToRecFN.scala 120:26]
  wire [25:0] _T_149 = _T_136 & _T_145 ? roundMask[26:1] : 26'h0; // @[RoundRawFNToRecFN.scala 119:21]
  wire [25:0] _T_150 = ~_T_149; // @[RoundRawFNToRecFN.scala 119:17]
  wire [25:0] _T_151 = _T_142 & _T_150; // @[RoundRawFNToRecFN.scala 118:55]
  wire [26:0] _T_152 = ~roundMask; // @[RoundRawFNToRecFN.scala 124:26]
  wire [26:0] _T_153 = io_in_sig & _T_152; // @[RoundRawFNToRecFN.scala 124:24]
  wire [25:0] roundedSig = roundingMode_nearest_even & roundPosBit | _T_137 ? _T_151 : {{1'd0}, _T_153[26:2]}; // @[RoundRawFNToRecFN.scala 116:12]
  wire [2:0] _T_156 = {1'b0,$signed(roundedSig[25:24])}; // @[RoundRawFNToRecFN.scala 127:60]
  wire [9:0] _GEN_4 = {{7{_T_156[2]}},_T_156}; // @[RoundRawFNToRecFN.scala 127:34]
  wire [10:0] sRoundedExp = $signed(io_in_sExp) + $signed(_GEN_4); // @[RoundRawFNToRecFN.scala 127:34]
  wire [8:0] common_expOut = sRoundedExp[8:0]; // @[RoundRawFNToRecFN.scala 129:36]
  wire [22:0] common_fractOut = doShiftSigDown1 ? roundedSig[23:1] : roundedSig[22:0]; // @[RoundRawFNToRecFN.scala 131:12]
  wire [3:0] _T_159 = sRoundedExp[10:7]; // @[RoundRawFNToRecFN.scala 136:39]
  wire  common_overflow = $signed(_T_159) >= 4'sh3; // @[RoundRawFNToRecFN.scala 136:56]
  wire  common_totalUnderflow = $signed(sRoundedExp) < 11'sh6b; // @[RoundRawFNToRecFN.scala 138:46]
  wire [8:0] _T_164 = doShiftSigDown1 ? $signed(9'sh81) : $signed(9'sh82); // @[RoundRawFNToRecFN.scala 142:21]
  wire [9:0] _GEN_5 = {{1{_T_164[8]}},_T_164}; // @[RoundRawFNToRecFN.scala 141:25]
  wire  _T_165 = $signed(io_in_sExp) < $signed(_GEN_5); // @[RoundRawFNToRecFN.scala 141:25]
  wire  common_underflow = anyRound & _T_165; // @[RoundRawFNToRecFN.scala 140:18]
  wire  isNaNOut = io_invalidExc | io_in_isNaN; // @[RoundRawFNToRecFN.scala 147:34]
  wire  commonCase = ~isNaNOut & ~io_in_isInf & ~io_in_isZero; // @[RoundRawFNToRecFN.scala 149:61]
  wire  overflow = commonCase & common_overflow; // @[RoundRawFNToRecFN.scala 150:32]
  wire  underflow = commonCase & common_underflow; // @[RoundRawFNToRecFN.scala 151:32]
  wire  inexact = overflow | commonCase & anyRound; // @[RoundRawFNToRecFN.scala 152:28]
  wire  overflow_roundMagUp = roundingMode_nearest_even | roundMagUp; // @[RoundRawFNToRecFN.scala 154:57]
  wire  pegMinNonzeroMagOut = commonCase & common_totalUnderflow & roundMagUp; // @[RoundRawFNToRecFN.scala 155:67]
  wire  pegMaxFiniteMagOut = commonCase & overflow & ~overflow_roundMagUp; // @[RoundRawFNToRecFN.scala 156:53]
  wire  notNaN_isInfOut = io_in_isInf | overflow & overflow_roundMagUp; // @[RoundRawFNToRecFN.scala 158:32]
  wire  signOut = isNaNOut ? 1'h0 : io_in_sign; // @[RoundRawFNToRecFN.scala 160:22]
  wire [8:0] _T_183 = io_in_isZero | common_totalUnderflow ? 9'h1c0 : 9'h0; // @[RoundRawFNToRecFN.scala 163:18]
  wire [8:0] _T_184 = ~_T_183; // @[RoundRawFNToRecFN.scala 163:14]
  wire [8:0] _T_185 = common_expOut & _T_184; // @[RoundRawFNToRecFN.scala 162:24]
  wire [8:0] _T_189 = pegMinNonzeroMagOut ? 9'h194 : 9'h0; // @[RoundRawFNToRecFN.scala 167:18]
  wire [8:0] _T_190 = ~_T_189; // @[RoundRawFNToRecFN.scala 167:14]
  wire [8:0] _T_191 = _T_185 & _T_190; // @[RoundRawFNToRecFN.scala 166:17]
  wire [8:0] _T_194 = pegMaxFiniteMagOut ? 9'h80 : 9'h0; // @[RoundRawFNToRecFN.scala 171:18]
  wire [8:0] _T_195 = ~_T_194; // @[RoundRawFNToRecFN.scala 171:14]
  wire [8:0] _T_196 = _T_191 & _T_195; // @[RoundRawFNToRecFN.scala 170:17]
  wire [8:0] _T_199 = notNaN_isInfOut ? 9'h40 : 9'h0; // @[RoundRawFNToRecFN.scala 175:18]
  wire [8:0] _T_200 = ~_T_199; // @[RoundRawFNToRecFN.scala 175:14]
  wire [8:0] _T_201 = _T_196 & _T_200; // @[RoundRawFNToRecFN.scala 174:17]
  wire [8:0] _T_204 = pegMinNonzeroMagOut ? 9'h6b : 9'h0; // @[RoundRawFNToRecFN.scala 179:16]
  wire [8:0] _T_205 = _T_201 | _T_204; // @[RoundRawFNToRecFN.scala 178:18]
  wire [8:0] _T_208 = pegMaxFiniteMagOut ? 9'h17f : 9'h0; // @[RoundRawFNToRecFN.scala 183:16]
  wire [8:0] _T_209 = _T_205 | _T_208; // @[RoundRawFNToRecFN.scala 182:15]
  wire [8:0] _T_212 = notNaN_isInfOut ? 9'h180 : 9'h0; // @[RoundRawFNToRecFN.scala 187:16]
  wire [8:0] _T_213 = _T_209 | _T_212; // @[RoundRawFNToRecFN.scala 186:15]
  wire [8:0] _T_216 = isNaNOut ? 9'h1c0 : 9'h0; // @[RoundRawFNToRecFN.scala 188:16]
  wire [8:0] expOut = _T_213 | _T_216; // @[RoundRawFNToRecFN.scala 187:71]
  wire [22:0] _T_221 = isNaNOut ? 23'h400000 : 23'h0; // @[RoundRawFNToRecFN.scala 191:16]
  wire [22:0] _T_222 = common_totalUnderflow | isNaNOut ? _T_221 : common_fractOut; // @[RoundRawFNToRecFN.scala 190:12]
  wire [22:0] _T_226 = pegMaxFiniteMagOut ? 23'h7fffff : 23'h0; // @[Bitwise.scala 71:12]
  wire [22:0] fractOut = _T_222 | _T_226; // @[RoundRawFNToRecFN.scala 193:11]
  wire [9:0] _T_227 = {signOut,expOut}; // @[Cat.scala 30:58]
  wire [1:0] _T_229 = {underflow,inexact}; // @[Cat.scala 30:58]
  wire [2:0] _T_231 = {io_invalidExc,1'h0,overflow}; // @[Cat.scala 30:58]
  assign io_out = {_T_227,fractOut}; // @[Cat.scala 30:58]
  assign io_exceptionFlags = {_T_231,_T_229}; // @[Cat.scala 30:58]
endmodule
module MulAddRecFN_preMul(
  input  [1:0]  io_op,
  input  [32:0] io_a,
  input  [32:0] io_b,
  input  [32:0] io_c,
  input  [1:0]  io_roundingMode,
  output [23:0] io_mulAddA,
  output [23:0] io_mulAddB,
  output [47:0] io_mulAddC,
  output [2:0]  io_toPostMul_highExpA,
  output        io_toPostMul_isNaN_isQuietNaNA,
  output [2:0]  io_toPostMul_highExpB,
  output        io_toPostMul_isNaN_isQuietNaNB,
  output        io_toPostMul_signProd,
  output        io_toPostMul_isZeroProd,
  output        io_toPostMul_opSignC,
  output [2:0]  io_toPostMul_highExpC,
  output        io_toPostMul_isNaN_isQuietNaNC,
  output        io_toPostMul_isCDominant,
  output        io_toPostMul_CAlignDist_0,
  output [6:0]  io_toPostMul_CAlignDist,
  output        io_toPostMul_bit0AlignedNegSigC,
  output [25:0] io_toPostMul_highAlignedNegSigC,
  output [10:0] io_toPostMul_sExpSum,
  output [1:0]  io_toPostMul_roundingMode
);
  wire  signA = io_a[32]; // @[MulAddRecFN.scala 102:22]
  wire [8:0] expA = io_a[31:23]; // @[MulAddRecFN.scala 103:22]
  wire [22:0] fractA = io_a[22:0]; // @[MulAddRecFN.scala 104:22]
  wire  isZeroA = expA[8:6] == 3'h0; // @[MulAddRecFN.scala 105:49]
  wire  _T_55 = ~isZeroA; // @[MulAddRecFN.scala 106:20]
  wire  signB = io_b[32]; // @[MulAddRecFN.scala 108:22]
  wire [8:0] expB = io_b[31:23]; // @[MulAddRecFN.scala 109:22]
  wire [22:0] fractB = io_b[22:0]; // @[MulAddRecFN.scala 110:22]
  wire  isZeroB = expB[8:6] == 3'h0; // @[MulAddRecFN.scala 111:49]
  wire  _T_59 = ~isZeroB; // @[MulAddRecFN.scala 112:20]
  wire  opSignC = io_c[32] ^ io_op[0]; // @[MulAddRecFN.scala 114:45]
  wire [8:0] expC = io_c[31:23]; // @[MulAddRecFN.scala 115:22]
  wire [22:0] fractC = io_c[22:0]; // @[MulAddRecFN.scala 116:22]
  wire  isZeroC = expC[8:6] == 3'h0; // @[MulAddRecFN.scala 117:49]
  wire  _T_65 = ~isZeroC; // @[MulAddRecFN.scala 118:20]
  wire [23:0] sigC = {_T_65,fractC}; // @[Cat.scala 30:58]
  wire  signProd = signA ^ signB ^ io_op[1]; // @[MulAddRecFN.scala 122:34]
  wire  isZeroProd = isZeroA | isZeroB; // @[MulAddRecFN.scala 123:30]
  wire  _T_70 = ~expB[8]; // @[MulAddRecFN.scala 125:28]
  wire [2:0] _T_74 = _T_70 ? 3'h7 : 3'h0; // @[Bitwise.scala 71:12]
  wire [10:0] _T_76 = {_T_74,expB[7:0]}; // @[Cat.scala 30:58]
  wire [10:0] _GEN_0 = {{2'd0}, expA}; // @[MulAddRecFN.scala 125:14]
  wire [10:0] _T_78 = _GEN_0 + _T_76; // @[MulAddRecFN.scala 125:14]
  wire [10:0] sExpAlignedProd = _T_78 + 11'h1b; // @[MulAddRecFN.scala 125:70]
  wire  doSubMags = signProd ^ opSignC; // @[MulAddRecFN.scala 130:30]
  wire [10:0] _GEN_1 = {{2'd0}, expC}; // @[MulAddRecFN.scala 132:42]
  wire [10:0] sNatCAlignDist = sExpAlignedProd - _GEN_1; // @[MulAddRecFN.scala 132:42]
  wire  CAlignDist_floor = isZeroProd | sNatCAlignDist[10]; // @[MulAddRecFN.scala 133:39]
  wire  _T_91 = sNatCAlignDist[9:0] < 10'h19; // @[MulAddRecFN.scala 139:51]
  wire  _T_92 = CAlignDist_floor | _T_91; // @[MulAddRecFN.scala 138:31]
  wire [6:0] _T_99 = sNatCAlignDist[9:0] < 10'h4a ? sNatCAlignDist[6:0] : 7'h4a; // @[MulAddRecFN.scala 143:16]
  wire [6:0] CAlignDist = CAlignDist_floor ? 7'h0 : _T_99; // @[MulAddRecFN.scala 141:12]
  wire [64:0] _T_103 = 65'sh10000000000000000 >>> CAlignDist[5:0]; // @[primitives.scala 68:52]
  wire [7:0] _T_111 = {{4'd0}, _T_103[61:58]}; // @[Bitwise.scala 102:31]
  wire [7:0] _T_113 = {_T_103[57:54], 4'h0}; // @[Bitwise.scala 102:65]
  wire [7:0] _T_115 = _T_113 & 8'hf0; // @[Bitwise.scala 102:75]
  wire [7:0] _T_116 = _T_111 | _T_115; // @[Bitwise.scala 102:39]
  wire [7:0] _GEN_2 = {{2'd0}, _T_116[7:2]}; // @[Bitwise.scala 102:31]
  wire [7:0] _T_121 = _GEN_2 & 8'h33; // @[Bitwise.scala 102:31]
  wire [7:0] _T_123 = {_T_116[5:0], 2'h0}; // @[Bitwise.scala 102:65]
  wire [7:0] _T_125 = _T_123 & 8'hcc; // @[Bitwise.scala 102:75]
  wire [7:0] _T_126 = _T_121 | _T_125; // @[Bitwise.scala 102:39]
  wire [7:0] _GEN_3 = {{1'd0}, _T_126[7:1]}; // @[Bitwise.scala 102:31]
  wire [7:0] _T_131 = _GEN_3 & 8'h55; // @[Bitwise.scala 102:31]
  wire [7:0] _T_133 = {_T_126[6:0], 1'h0}; // @[Bitwise.scala 102:65]
  wire [7:0] _T_135 = _T_133 & 8'haa; // @[Bitwise.scala 102:75]
  wire [7:0] _T_136 = _T_131 | _T_135; // @[Bitwise.scala 102:39]
  wire [23:0] _T_143 = {_T_136,_T_103[62],_T_103[63],14'h3fff}; // @[Cat.scala 30:58]
  wire [7:0] _T_153 = {{4'd0}, _T_103[7:4]}; // @[Bitwise.scala 102:31]
  wire [7:0] _T_155 = {_T_103[3:0], 4'h0}; // @[Bitwise.scala 102:65]
  wire [7:0] _T_157 = _T_155 & 8'hf0; // @[Bitwise.scala 102:75]
  wire [7:0] _T_158 = _T_153 | _T_157; // @[Bitwise.scala 102:39]
  wire [7:0] _GEN_4 = {{2'd0}, _T_158[7:2]}; // @[Bitwise.scala 102:31]
  wire [7:0] _T_163 = _GEN_4 & 8'h33; // @[Bitwise.scala 102:31]
  wire [7:0] _T_165 = {_T_158[5:0], 2'h0}; // @[Bitwise.scala 102:65]
  wire [7:0] _T_167 = _T_165 & 8'hcc; // @[Bitwise.scala 102:75]
  wire [7:0] _T_168 = _T_163 | _T_167; // @[Bitwise.scala 102:39]
  wire [7:0] _GEN_5 = {{1'd0}, _T_168[7:1]}; // @[Bitwise.scala 102:31]
  wire [7:0] _T_173 = _GEN_5 & 8'h55; // @[Bitwise.scala 102:31]
  wire [7:0] _T_175 = {_T_168[6:0], 1'h0}; // @[Bitwise.scala 102:65]
  wire [7:0] _T_177 = _T_175 & 8'haa; // @[Bitwise.scala 102:75]
  wire [7:0] _T_178 = _T_173 | _T_177; // @[Bitwise.scala 102:39]
  wire [13:0] _T_195 = {_T_178,_T_103[8],_T_103[9],_T_103[10],_T_103[11],_T_103[12],_T_103[13]}; // @[Cat.scala 30:58]
  wire [23:0] CExtraMask = CAlignDist[6] ? _T_143 : {{10'd0}, _T_195}; // @[primitives.scala 61:20]
  wire [23:0] _T_196 = ~sigC; // @[MulAddRecFN.scala 151:34]
  wire [23:0] negSigC = doSubMags ? _T_196 : sigC; // @[MulAddRecFN.scala 151:22]
  wire [49:0] _T_200 = doSubMags ? 50'h3ffffffffffff : 50'h0; // @[Bitwise.scala 71:12]
  wire [74:0] _T_203 = {doSubMags,negSigC,_T_200}; // @[MulAddRecFN.scala 154:64]
  wire [23:0] _T_205 = sigC & CExtraMask; // @[MulAddRecFN.scala 156:19]
  wire  _T_208 = _T_205 != 24'h0 ^ doSubMags; // @[MulAddRecFN.scala 156:37]
  wire [74:0] _T_209 = $signed(_T_203) >>> CAlignDist; // @[Cat.scala 30:58]
  wire [75:0] _T_210 = {_T_209,_T_208}; // @[Cat.scala 30:58]
  wire [74:0] alignedNegSigC = _T_210[74:0]; // @[MulAddRecFN.scala 157:10]
  assign io_mulAddA = {_T_55,fractA}; // @[Cat.scala 30:58]
  assign io_mulAddB = {_T_59,fractB}; // @[Cat.scala 30:58]
  assign io_mulAddC = alignedNegSigC[48:1]; // @[MulAddRecFN.scala 161:33]
  assign io_toPostMul_highExpA = expA[8:6]; // @[MulAddRecFN.scala 163:44]
  assign io_toPostMul_isNaN_isQuietNaNA = fractA[22]; // @[MulAddRecFN.scala 164:46]
  assign io_toPostMul_highExpB = expB[8:6]; // @[MulAddRecFN.scala 165:44]
  assign io_toPostMul_isNaN_isQuietNaNB = fractB[22]; // @[MulAddRecFN.scala 166:46]
  assign io_toPostMul_signProd = signA ^ signB ^ io_op[1]; // @[MulAddRecFN.scala 122:34]
  assign io_toPostMul_isZeroProd = isZeroA | isZeroB; // @[MulAddRecFN.scala 123:30]
  assign io_toPostMul_opSignC = io_c[32] ^ io_op[0]; // @[MulAddRecFN.scala 114:45]
  assign io_toPostMul_highExpC = expC[8:6]; // @[MulAddRecFN.scala 170:44]
  assign io_toPostMul_isNaN_isQuietNaNC = fractC[22]; // @[MulAddRecFN.scala 171:46]
  assign io_toPostMul_isCDominant = _T_65 & _T_92; // @[MulAddRecFN.scala 137:19]
  assign io_toPostMul_CAlignDist_0 = CAlignDist_floor | sNatCAlignDist[9:0] == 10'h0; // @[MulAddRecFN.scala 135:26]
  assign io_toPostMul_CAlignDist = CAlignDist_floor ? 7'h0 : _T_99; // @[MulAddRecFN.scala 141:12]
  assign io_toPostMul_bit0AlignedNegSigC = alignedNegSigC[0]; // @[MulAddRecFN.scala 175:54]
  assign io_toPostMul_highAlignedNegSigC = alignedNegSigC[74:49]; // @[MulAddRecFN.scala 177:23]
  assign io_toPostMul_sExpSum = CAlignDist_floor ? {{2'd0}, expC} : sExpAlignedProd; // @[MulAddRecFN.scala 148:22]
  assign io_toPostMul_roundingMode = io_roundingMode; // @[MulAddRecFN.scala 179:37]
endmodule
module MulAddRecFN_postMul(
  input  [2:0]  io_fromPreMul_highExpA,
  input         io_fromPreMul_isNaN_isQuietNaNA,
  input  [2:0]  io_fromPreMul_highExpB,
  input         io_fromPreMul_isNaN_isQuietNaNB,
  input         io_fromPreMul_signProd,
  input         io_fromPreMul_isZeroProd,
  input         io_fromPreMul_opSignC,
  input  [2:0]  io_fromPreMul_highExpC,
  input         io_fromPreMul_isNaN_isQuietNaNC,
  input         io_fromPreMul_isCDominant,
  input         io_fromPreMul_CAlignDist_0,
  input  [6:0]  io_fromPreMul_CAlignDist,
  input         io_fromPreMul_bit0AlignedNegSigC,
  input  [25:0] io_fromPreMul_highAlignedNegSigC,
  input  [10:0] io_fromPreMul_sExpSum,
  input  [1:0]  io_fromPreMul_roundingMode,
  input  [48:0] io_mulAddResult,
  output [32:0] io_out,
  output [4:0]  io_exceptionFlags
);
  wire  isZeroA = io_fromPreMul_highExpA == 3'h0; // @[MulAddRecFN.scala 207:46]
  wire  isSpecialA = io_fromPreMul_highExpA[2:1] == 2'h3; // @[MulAddRecFN.scala 208:52]
  wire  isInfA = isSpecialA & ~io_fromPreMul_highExpA[0]; // @[MulAddRecFN.scala 209:29]
  wire  isNaNA = isSpecialA & io_fromPreMul_highExpA[0]; // @[MulAddRecFN.scala 210:29]
  wire  isSigNaNA = isNaNA & ~io_fromPreMul_isNaN_isQuietNaNA; // @[MulAddRecFN.scala 211:28]
  wire  isZeroB = io_fromPreMul_highExpB == 3'h0; // @[MulAddRecFN.scala 213:46]
  wire  isSpecialB = io_fromPreMul_highExpB[2:1] == 2'h3; // @[MulAddRecFN.scala 214:52]
  wire  isInfB = isSpecialB & ~io_fromPreMul_highExpB[0]; // @[MulAddRecFN.scala 215:29]
  wire  isNaNB = isSpecialB & io_fromPreMul_highExpB[0]; // @[MulAddRecFN.scala 216:29]
  wire  isSigNaNB = isNaNB & ~io_fromPreMul_isNaN_isQuietNaNB; // @[MulAddRecFN.scala 217:28]
  wire  isZeroC = io_fromPreMul_highExpC == 3'h0; // @[MulAddRecFN.scala 219:46]
  wire  isSpecialC = io_fromPreMul_highExpC[2:1] == 2'h3; // @[MulAddRecFN.scala 220:52]
  wire  isInfC = isSpecialC & ~io_fromPreMul_highExpC[0]; // @[MulAddRecFN.scala 221:29]
  wire  isNaNC = isSpecialC & io_fromPreMul_highExpC[0]; // @[MulAddRecFN.scala 222:29]
  wire  isSigNaNC = isNaNC & ~io_fromPreMul_isNaN_isQuietNaNC; // @[MulAddRecFN.scala 223:28]
  wire  roundingMode_nearest_even = io_fromPreMul_roundingMode == 2'h0; // @[MulAddRecFN.scala 226:37]
  wire  roundingMode_min = io_fromPreMul_roundingMode == 2'h2; // @[MulAddRecFN.scala 228:59]
  wire  roundingMode_max = io_fromPreMul_roundingMode == 2'h3; // @[MulAddRecFN.scala 229:59]
  wire  doSubMags = io_fromPreMul_signProd ^ io_fromPreMul_opSignC; // @[MulAddRecFN.scala 232:44]
  wire [25:0] _T_74 = io_fromPreMul_highAlignedNegSigC + 26'h1; // @[MulAddRecFN.scala 238:50]
  wire [25:0] _T_75 = io_mulAddResult[48] ? _T_74 : io_fromPreMul_highAlignedNegSigC; // @[MulAddRecFN.scala 237:16]
  wire [74:0] sigSum = {_T_75,io_mulAddResult[47:0],io_fromPreMul_bit0AlignedNegSigC}; // @[Cat.scala 30:58]
  wire [50:0] _T_82 = {sigSum[50:1], 1'h0}; // @[MulAddRecFN.scala 191:41]
  wire [50:0] _GEN_0 = {{1'd0}, sigSum[50:1]}; // @[MulAddRecFN.scala 191:32]
  wire [50:0] _T_83 = _GEN_0 ^ _T_82; // @[MulAddRecFN.scala 191:32]
  wire  _T_89 = _T_83[49:32] != 18'h0; // @[CircuitMath.scala 37:22]
  wire  _T_93 = _T_83[49:48] != 2'h0; // @[CircuitMath.scala 37:22]
  wire  _T_98 = _T_83[47:40] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_102 = _T_83[47:44] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_108 = _T_83[46] ? 2'h2 : {{1'd0}, _T_83[45]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_109 = _T_83[47] ? 2'h3 : _T_108; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_115 = _T_83[42] ? 2'h2 : {{1'd0}, _T_83[41]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_116 = _T_83[43] ? 2'h3 : _T_115; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_117 = _T_102 ? _T_109 : _T_116; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_118 = {_T_102,_T_117}; // @[Cat.scala 30:58]
  wire  _T_122 = _T_83[39:36] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_128 = _T_83[38] ? 2'h2 : {{1'd0}, _T_83[37]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_129 = _T_83[39] ? 2'h3 : _T_128; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_135 = _T_83[34] ? 2'h2 : {{1'd0}, _T_83[33]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_136 = _T_83[35] ? 2'h3 : _T_135; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_137 = _T_122 ? _T_129 : _T_136; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_138 = {_T_122,_T_137}; // @[Cat.scala 30:58]
  wire [2:0] _T_139 = _T_98 ? _T_118 : _T_138; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_140 = {_T_98,_T_139}; // @[Cat.scala 30:58]
  wire [3:0] _T_141 = _T_93 ? {{3'd0}, _T_83[49]} : _T_140; // @[CircuitMath.scala 38:21]
  wire [4:0] _T_142 = {_T_93,_T_141}; // @[Cat.scala 30:58]
  wire  _T_146 = _T_83[31:16] != 16'h0; // @[CircuitMath.scala 37:22]
  wire  _T_150 = _T_83[31:24] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_154 = _T_83[31:28] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_160 = _T_83[30] ? 2'h2 : {{1'd0}, _T_83[29]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_161 = _T_83[31] ? 2'h3 : _T_160; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_167 = _T_83[26] ? 2'h2 : {{1'd0}, _T_83[25]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_168 = _T_83[27] ? 2'h3 : _T_167; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_169 = _T_154 ? _T_161 : _T_168; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_170 = {_T_154,_T_169}; // @[Cat.scala 30:58]
  wire  _T_174 = _T_83[23:20] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_180 = _T_83[22] ? 2'h2 : {{1'd0}, _T_83[21]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_181 = _T_83[23] ? 2'h3 : _T_180; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_187 = _T_83[18] ? 2'h2 : {{1'd0}, _T_83[17]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_188 = _T_83[19] ? 2'h3 : _T_187; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_189 = _T_174 ? _T_181 : _T_188; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_190 = {_T_174,_T_189}; // @[Cat.scala 30:58]
  wire [2:0] _T_191 = _T_150 ? _T_170 : _T_190; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_192 = {_T_150,_T_191}; // @[Cat.scala 30:58]
  wire  _T_196 = _T_83[15:8] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_200 = _T_83[15:12] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_206 = _T_83[14] ? 2'h2 : {{1'd0}, _T_83[13]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_207 = _T_83[15] ? 2'h3 : _T_206; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_213 = _T_83[10] ? 2'h2 : {{1'd0}, _T_83[9]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_214 = _T_83[11] ? 2'h3 : _T_213; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_215 = _T_200 ? _T_207 : _T_214; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_216 = {_T_200,_T_215}; // @[Cat.scala 30:58]
  wire  _T_220 = _T_83[7:4] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_226 = _T_83[6] ? 2'h2 : {{1'd0}, _T_83[5]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_227 = _T_83[7] ? 2'h3 : _T_226; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_233 = _T_83[2] ? 2'h2 : {{1'd0}, _T_83[1]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_234 = _T_83[3] ? 2'h3 : _T_233; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_235 = _T_220 ? _T_227 : _T_234; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_236 = {_T_220,_T_235}; // @[Cat.scala 30:58]
  wire [2:0] _T_237 = _T_196 ? _T_216 : _T_236; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_238 = {_T_196,_T_237}; // @[Cat.scala 30:58]
  wire [3:0] _T_239 = _T_146 ? _T_192 : _T_238; // @[CircuitMath.scala 38:21]
  wire [4:0] _T_240 = {_T_146,_T_239}; // @[Cat.scala 30:58]
  wire [4:0] _T_241 = _T_89 ? _T_142 : _T_240; // @[CircuitMath.scala 38:21]
  wire [5:0] _T_242 = {_T_89,_T_241}; // @[Cat.scala 30:58]
  wire [6:0] _GEN_1 = {{1'd0}, _T_242}; // @[primitives.scala 79:25]
  wire [6:0] estNormNeg_dist = 7'h49 - _GEN_1; // @[primitives.scala 79:25]
  wire  _T_247 = sigSum[33:18] != 16'h0; // @[MulAddRecFN.scala 254:15]
  wire  _T_250 = sigSum[17:0] != 18'h0; // @[MulAddRecFN.scala 255:57]
  wire [1:0] firstReduceSigSum = {_T_247,_T_250}; // @[Cat.scala 30:58]
  wire [74:0] complSigSum = ~sigSum; // @[MulAddRecFN.scala 257:23]
  wire  _T_253 = complSigSum[33:18] != 16'h0; // @[MulAddRecFN.scala 261:15]
  wire  _T_256 = complSigSum[17:0] != 18'h0; // @[MulAddRecFN.scala 262:62]
  wire [1:0] firstReduceComplSigSum = {_T_253,_T_256}; // @[Cat.scala 30:58]
  wire [6:0] _T_261 = io_fromPreMul_CAlignDist - 7'h1; // @[MulAddRecFN.scala 268:39]
  wire [6:0] CDom_estNormDist = io_fromPreMul_CAlignDist_0 | doSubMags ? io_fromPreMul_CAlignDist : {{2'd0}, _T_261[4:0]
    }; // @[MulAddRecFN.scala 266:12]
  wire  _T_264 = ~doSubMags; // @[MulAddRecFN.scala 271:13]
  wire  _T_267 = ~CDom_estNormDist[4]; // @[MulAddRecFN.scala 271:28]
  wire  _T_271 = firstReduceSigSum != 2'h0; // @[MulAddRecFN.scala 273:35]
  wire [41:0] _T_272 = {sigSum[74:34],_T_271}; // @[Cat.scala 30:58]
  wire [41:0] _T_274 = ~doSubMags & ~CDom_estNormDist[4] ? _T_272 : 42'h0; // @[MulAddRecFN.scala 271:12]
  wire [41:0] _T_281 = {sigSum[58:18],firstReduceSigSum[0]}; // @[Cat.scala 30:58]
  wire [41:0] _T_283 = _T_264 & CDom_estNormDist[4] ? _T_281 : 42'h0; // @[MulAddRecFN.scala 277:12]
  wire [41:0] _T_284 = _T_274 | _T_283; // @[MulAddRecFN.scala 276:11]
  wire  _T_291 = firstReduceComplSigSum != 2'h0; // @[MulAddRecFN.scala 288:40]
  wire [41:0] _T_292 = {complSigSum[74:34],_T_291}; // @[Cat.scala 30:58]
  wire [41:0] _T_294 = doSubMags & _T_267 ? _T_292 : 42'h0; // @[MulAddRecFN.scala 286:12]
  wire [41:0] _T_295 = _T_284 | _T_294; // @[MulAddRecFN.scala 285:11]
  wire [41:0] _T_300 = {complSigSum[58:18],firstReduceComplSigSum[0]}; // @[Cat.scala 30:58]
  wire [41:0] _T_302 = doSubMags & CDom_estNormDist[4] ? _T_300 : 42'h0; // @[MulAddRecFN.scala 292:12]
  wire [41:0] CDom_firstNormAbsSigSum = _T_295 | _T_302; // @[MulAddRecFN.scala 291:11]
  wire  _T_306 = ~firstReduceComplSigSum[0]; // @[MulAddRecFN.scala 310:21]
  wire  _T_308 = doSubMags ? _T_306 : firstReduceSigSum[0]; // @[MulAddRecFN.scala 309:20]
  wire [33:0] _T_309 = {sigSum[50:18],_T_308}; // @[Cat.scala 30:58]
  wire [15:0] _T_317 = doSubMags ? 16'hffff : 16'h0; // @[Bitwise.scala 71:12]
  wire [41:0] _T_318 = {sigSum[26:1],_T_317}; // @[Cat.scala 30:58]
  wire [41:0] _T_319 = estNormNeg_dist[4] ? _T_318 : sigSum[42:1]; // @[MulAddRecFN.scala 339:17]
  wire [31:0] _T_325 = doSubMags ? 32'hffffffff : 32'h0; // @[Bitwise.scala 71:12]
  wire [41:0] _T_326 = {sigSum[10:1],_T_325}; // @[Cat.scala 30:58]
  wire [41:0] _T_327 = estNormNeg_dist[4] ? {{8'd0}, _T_309} : _T_326; // @[MulAddRecFN.scala 345:17]
  wire [41:0] notCDom_pos_firstNormAbsSigSum = estNormNeg_dist[5] ? _T_319 : _T_327; // @[MulAddRecFN.scala 338:12]
  wire [32:0] _T_330 = {complSigSum[49:18],firstReduceComplSigSum[0]}; // @[Cat.scala 30:58]
  wire [42:0] _T_335 = {complSigSum[27:1], 16'h0}; // @[MulAddRecFN.scala 381:64]
  wire [42:0] _T_336 = estNormNeg_dist[4] ? _T_335 : {{1'd0}, complSigSum[42:1]}; // @[MulAddRecFN.scala 380:17]
  wire [42:0] _T_339 = {complSigSum[11:1], 32'h0}; // @[MulAddRecFN.scala 387:64]
  wire [42:0] _T_340 = estNormNeg_dist[4] ? {{10'd0}, _T_330} : _T_339; // @[MulAddRecFN.scala 385:17]
  wire [42:0] notCDom_neg_cFirstNormAbsSigSum = estNormNeg_dist[5] ? _T_336 : _T_340; // @[MulAddRecFN.scala 379:12]
  wire  notCDom_signSigSum = sigSum[51]; // @[MulAddRecFN.scala 392:36]
  wire  _T_343 = doSubMags & ~isZeroC; // @[MulAddRecFN.scala 395:23]
  wire  doNegSignSum = io_fromPreMul_isCDominant ? _T_343 : notCDom_signSigSum; // @[MulAddRecFN.scala 394:12]
  wire [6:0] estNormDist = io_fromPreMul_isCDominant ? CDom_estNormDist : estNormNeg_dist; // @[MulAddRecFN.scala 399:12]
  wire [42:0] _T_345 = io_fromPreMul_isCDominant ? {{1'd0}, CDom_firstNormAbsSigSum} : notCDom_neg_cFirstNormAbsSigSum; // @[MulAddRecFN.scala 408:16]
  wire [41:0] _T_346 = io_fromPreMul_isCDominant ? CDom_firstNormAbsSigSum : notCDom_pos_firstNormAbsSigSum; // @[MulAddRecFN.scala 412:16]
  wire [42:0] cFirstNormAbsSigSum = notCDom_signSigSum ? _T_345 : {{1'd0}, _T_346}; // @[MulAddRecFN.scala 407:12]
  wire  doIncrSig = ~io_fromPreMul_isCDominant & ~notCDom_signSigSum & doSubMags; // @[MulAddRecFN.scala 418:61]
  wire [3:0] estNormDist_5 = estNormDist[3:0]; // @[MulAddRecFN.scala 419:36]
  wire [3:0] normTo2ShiftDist = ~estNormDist_5; // @[MulAddRecFN.scala 420:28]
  wire [16:0] _T_353 = 17'sh10000 >>> normTo2ShiftDist; // @[primitives.scala 68:52]
  wire [7:0] _T_361 = {{4'd0}, _T_353[8:5]}; // @[Bitwise.scala 102:31]
  wire [7:0] _T_363 = {_T_353[4:1], 4'h0}; // @[Bitwise.scala 102:65]
  wire [7:0] _T_365 = _T_363 & 8'hf0; // @[Bitwise.scala 102:75]
  wire [7:0] _T_366 = _T_361 | _T_365; // @[Bitwise.scala 102:39]
  wire [7:0] _GEN_2 = {{2'd0}, _T_366[7:2]}; // @[Bitwise.scala 102:31]
  wire [7:0] _T_371 = _GEN_2 & 8'h33; // @[Bitwise.scala 102:31]
  wire [7:0] _T_373 = {_T_366[5:0], 2'h0}; // @[Bitwise.scala 102:65]
  wire [7:0] _T_375 = _T_373 & 8'hcc; // @[Bitwise.scala 102:75]
  wire [7:0] _T_376 = _T_371 | _T_375; // @[Bitwise.scala 102:39]
  wire [7:0] _GEN_3 = {{1'd0}, _T_376[7:1]}; // @[Bitwise.scala 102:31]
  wire [7:0] _T_381 = _GEN_3 & 8'h55; // @[Bitwise.scala 102:31]
  wire [7:0] _T_383 = {_T_376[6:0], 1'h0}; // @[Bitwise.scala 102:65]
  wire [7:0] _T_385 = _T_383 & 8'haa; // @[Bitwise.scala 102:75]
  wire [7:0] _T_386 = _T_381 | _T_385; // @[Bitwise.scala 102:39]
  wire [15:0] absSigSumExtraMask = {_T_386,_T_353[9],_T_353[10],_T_353[11],_T_353[12],_T_353[13],_T_353[14],_T_353[15],1'h1
    }; // @[Cat.scala 30:58]
  wire [41:0] _T_409 = cFirstNormAbsSigSum[42:1] >> normTo2ShiftDist; // @[MulAddRecFN.scala 424:65]
  wire [15:0] _T_411 = ~cFirstNormAbsSigSum[15:0]; // @[MulAddRecFN.scala 427:19]
  wire [15:0] _T_412 = _T_411 & absSigSumExtraMask; // @[MulAddRecFN.scala 427:62]
  wire  _T_414 = _T_412 == 16'h0; // @[MulAddRecFN.scala 428:43]
  wire [15:0] _T_416 = cFirstNormAbsSigSum[15:0] & absSigSumExtraMask; // @[MulAddRecFN.scala 430:61]
  wire  _T_418 = _T_416 != 16'h0; // @[MulAddRecFN.scala 431:43]
  wire  _T_419 = doIncrSig ? _T_414 : _T_418; // @[MulAddRecFN.scala 426:16]
  wire [42:0] _T_420 = {_T_409,_T_419}; // @[Cat.scala 30:58]
  wire [27:0] sigX3 = _T_420[27:0]; // @[MulAddRecFN.scala 434:10]
  wire  sigX3Shift1 = sigX3[27:26] == 2'h0; // @[MulAddRecFN.scala 436:58]
  wire [10:0] _GEN_4 = {{4'd0}, estNormDist}; // @[MulAddRecFN.scala 437:40]
  wire [10:0] sExpX3 = io_fromPreMul_sExpSum - _GEN_4; // @[MulAddRecFN.scala 437:40]
  wire  isZeroY = sigX3[27:25] == 3'h0; // @[MulAddRecFN.scala 439:54]
  wire  _T_427 = io_fromPreMul_signProd ^ doNegSignSum; // @[MulAddRecFN.scala 444:36]
  wire  signY = isZeroY ? roundingMode_min : _T_427; // @[MulAddRecFN.scala 442:12]
  wire [9:0] sExpX3_13 = sExpX3[9:0]; // @[MulAddRecFN.scala 446:27]
  wire [26:0] _T_432 = sExpX3[10] ? 27'h7ffffff : 27'h0; // @[Bitwise.scala 71:12]
  wire [9:0] _T_433 = ~sExpX3_13; // @[primitives.scala 50:21]
  wire [64:0] _T_444 = 65'sh10000000000000000 >>> _T_433[5:0]; // @[primitives.scala 68:52]
  wire [15:0] _T_452 = {{8'd0}, _T_444[58:51]}; // @[Bitwise.scala 102:31]
  wire [15:0] _T_454 = {_T_444[50:43], 8'h0}; // @[Bitwise.scala 102:65]
  wire [15:0] _T_456 = _T_454 & 16'hff00; // @[Bitwise.scala 102:75]
  wire [15:0] _T_457 = _T_452 | _T_456; // @[Bitwise.scala 102:39]
  wire [15:0] _GEN_5 = {{4'd0}, _T_457[15:4]}; // @[Bitwise.scala 102:31]
  wire [15:0] _T_462 = _GEN_5 & 16'hf0f; // @[Bitwise.scala 102:31]
  wire [15:0] _T_464 = {_T_457[11:0], 4'h0}; // @[Bitwise.scala 102:65]
  wire [15:0] _T_466 = _T_464 & 16'hf0f0; // @[Bitwise.scala 102:75]
  wire [15:0] _T_467 = _T_462 | _T_466; // @[Bitwise.scala 102:39]
  wire [15:0] _GEN_6 = {{2'd0}, _T_467[15:2]}; // @[Bitwise.scala 102:31]
  wire [15:0] _T_472 = _GEN_6 & 16'h3333; // @[Bitwise.scala 102:31]
  wire [15:0] _T_474 = {_T_467[13:0], 2'h0}; // @[Bitwise.scala 102:65]
  wire [15:0] _T_476 = _T_474 & 16'hcccc; // @[Bitwise.scala 102:75]
  wire [15:0] _T_477 = _T_472 | _T_476; // @[Bitwise.scala 102:39]
  wire [15:0] _GEN_7 = {{1'd0}, _T_477[15:1]}; // @[Bitwise.scala 102:31]
  wire [15:0] _T_482 = _GEN_7 & 16'h5555; // @[Bitwise.scala 102:31]
  wire [15:0] _T_484 = {_T_477[14:0], 1'h0}; // @[Bitwise.scala 102:65]
  wire [15:0] _T_486 = _T_484 & 16'haaaa; // @[Bitwise.scala 102:75]
  wire [15:0] _T_487 = _T_482 | _T_486; // @[Bitwise.scala 102:39]
  wire [20:0] _T_501 = {_T_487,_T_444[59],_T_444[60],_T_444[61],_T_444[62],_T_444[63]}; // @[Cat.scala 30:58]
  wire [20:0] _T_502 = ~_T_501; // @[primitives.scala 65:36]
  wire [20:0] _T_503 = _T_433[6] ? 21'h0 : _T_502; // @[primitives.scala 65:21]
  wire [20:0] _T_504 = ~_T_503; // @[primitives.scala 65:17]
  wire [24:0] _T_506 = {_T_504,4'hf}; // @[Cat.scala 30:58]
  wire [3:0] _T_520 = {_T_444[0],_T_444[1],_T_444[2],_T_444[3]}; // @[Cat.scala 30:58]
  wire [3:0] _T_522 = _T_433[6] ? _T_520 : 4'h0; // @[primitives.scala 59:20]
  wire [24:0] _T_523 = _T_433[7] ? _T_506 : {{21'd0}, _T_522}; // @[primitives.scala 61:20]
  wire [24:0] _T_525 = _T_433[8] ? _T_523 : 25'h0; // @[primitives.scala 59:20]
  wire [24:0] _T_527 = _T_433[9] ? _T_525 : 25'h0; // @[primitives.scala 59:20]
  wire [24:0] _GEN_8 = {{24'd0}, sigX3[26]}; // @[MulAddRecFN.scala 449:75]
  wire [24:0] _T_529 = _T_527 | _GEN_8; // @[MulAddRecFN.scala 449:75]
  wire [26:0] _T_531 = {_T_529,2'h3}; // @[Cat.scala 30:58]
  wire [26:0] roundMask = _T_432 | _T_531; // @[MulAddRecFN.scala 448:50]
  wire [25:0] _T_533 = ~roundMask[26:1]; // @[MulAddRecFN.scala 454:24]
  wire [26:0] _GEN_9 = {{1'd0}, _T_533}; // @[MulAddRecFN.scala 454:40]
  wire [26:0] roundPosMask = _GEN_9 & roundMask; // @[MulAddRecFN.scala 454:40]
  wire [27:0] _GEN_10 = {{1'd0}, roundPosMask}; // @[MulAddRecFN.scala 455:30]
  wire [27:0] _T_534 = sigX3 & _GEN_10; // @[MulAddRecFN.scala 455:30]
  wire  roundPosBit = _T_534 != 28'h0; // @[MulAddRecFN.scala 455:46]
  wire [27:0] _GEN_11 = {{2'd0}, roundMask[26:1]}; // @[MulAddRecFN.scala 456:34]
  wire [27:0] _T_537 = sigX3 & _GEN_11; // @[MulAddRecFN.scala 456:34]
  wire  anyRoundExtra = _T_537 != 28'h0; // @[MulAddRecFN.scala 456:50]
  wire [27:0] _T_539 = ~sigX3; // @[MulAddRecFN.scala 457:27]
  wire [27:0] _T_541 = _T_539 & _GEN_11; // @[MulAddRecFN.scala 457:34]
  wire  allRoundExtra = _T_541 == 28'h0; // @[MulAddRecFN.scala 457:50]
  wire  anyRound = roundPosBit | anyRoundExtra; // @[MulAddRecFN.scala 458:32]
  wire  allRound = roundPosBit & allRoundExtra; // @[MulAddRecFN.scala 459:32]
  wire  roundDirectUp = signY ? roundingMode_min : roundingMode_max; // @[MulAddRecFN.scala 460:28]
  wire  _T_544 = ~doIncrSig; // @[MulAddRecFN.scala 462:10]
  wire  _T_546 = ~doIncrSig & roundingMode_nearest_even & roundPosBit; // @[MulAddRecFN.scala 462:51]
  wire  _T_551 = _T_544 & roundDirectUp & anyRound; // @[MulAddRecFN.scala 464:49]
  wire  _T_552 = _T_546 & anyRoundExtra | _T_551; // @[MulAddRecFN.scala 463:78]
  wire  _T_553 = doIncrSig & allRound; // @[MulAddRecFN.scala 465:49]
  wire  _T_554 = _T_552 | _T_553; // @[MulAddRecFN.scala 464:65]
  wire  _T_556 = doIncrSig & roundingMode_nearest_even & roundPosBit; // @[MulAddRecFN.scala 466:49]
  wire  _T_557 = _T_554 | _T_556; // @[MulAddRecFN.scala 465:65]
  wire  _T_558 = doIncrSig & roundDirectUp; // @[MulAddRecFN.scala 467:20]
  wire  roundUp = _T_557 | _T_558; // @[MulAddRecFN.scala 466:65]
  wire  _T_564 = roundingMode_nearest_even & ~roundPosBit & allRoundExtra; // @[MulAddRecFN.scala 470:56]
  wire  _T_568 = roundingMode_nearest_even & roundPosBit & ~anyRoundExtra; // @[MulAddRecFN.scala 471:56]
  wire  roundEven = doIncrSig ? _T_564 : _T_568; // @[MulAddRecFN.scala 469:12]
  wire  inexactY = doIncrSig ? ~allRound : anyRound; // @[MulAddRecFN.scala 473:27]
  wire [27:0] _GEN_13 = {{1'd0}, roundMask}; // @[MulAddRecFN.scala 475:18]
  wire [27:0] _T_571 = sigX3 | _GEN_13; // @[MulAddRecFN.scala 475:18]
  wire [25:0] roundUp_sigY3 = _T_571[27:2] + 26'h1; // @[MulAddRecFN.scala 475:35]
  wire [26:0] _T_581 = ~roundMask; // @[MulAddRecFN.scala 477:48]
  wire [27:0] _GEN_14 = {{1'd0}, _T_581}; // @[MulAddRecFN.scala 477:46]
  wire [27:0] _T_582 = sigX3 & _GEN_14; // @[MulAddRecFN.scala 477:46]
  wire [25:0] _T_585 = ~roundUp & ~roundEven ? _T_582[27:2] : 26'h0; // @[MulAddRecFN.scala 477:12]
  wire [25:0] _T_587 = roundUp ? roundUp_sigY3 : 26'h0; // @[MulAddRecFN.scala 478:12]
  wire [25:0] _T_588 = _T_585 | _T_587; // @[MulAddRecFN.scala 477:79]
  wire [25:0] _T_591 = roundUp_sigY3 & _T_533; // @[MulAddRecFN.scala 479:51]
  wire [25:0] _T_593 = roundEven ? _T_591 : 26'h0; // @[MulAddRecFN.scala 479:12]
  wire [25:0] sigY3 = _T_588 | _T_593; // @[MulAddRecFN.scala 478:79]
  wire [10:0] _T_597 = sExpX3 + 11'h1; // @[MulAddRecFN.scala 482:41]
  wire [10:0] _T_599 = sigY3[25] ? _T_597 : 11'h0; // @[MulAddRecFN.scala 482:12]
  wire [10:0] _T_602 = sigY3[24] ? sExpX3 : 11'h0; // @[MulAddRecFN.scala 483:12]
  wire [10:0] _T_603 = _T_599 | _T_602; // @[MulAddRecFN.scala 482:61]
  wire [10:0] _T_610 = sExpX3 - 11'h1; // @[MulAddRecFN.scala 485:20]
  wire [10:0] _T_612 = sigY3[25:24] == 2'h0 ? _T_610 : 11'h0; // @[MulAddRecFN.scala 484:12]
  wire [10:0] sExpY = _T_603 | _T_612; // @[MulAddRecFN.scala 483:61]
  wire [8:0] expY = sExpY[8:0]; // @[MulAddRecFN.scala 488:21]
  wire [22:0] fractY = sigX3Shift1 ? sigY3[22:0] : sigY3[23:1]; // @[MulAddRecFN.scala 490:12]
  wire  overflowY = sExpY[9:7] == 3'h3; // @[MulAddRecFN.scala 492:56]
  wire  _T_623 = sExpY[9] | expY < 9'h6b; // @[MulAddRecFN.scala 496:34]
  wire  totalUnderflowY = ~isZeroY & _T_623; // @[MulAddRecFN.scala 495:19]
  wire [7:0] _T_627 = sigX3Shift1 ? 8'h82 : 8'h81; // @[MulAddRecFN.scala 501:26]
  wire [9:0] _GEN_15 = {{2'd0}, _T_627}; // @[MulAddRecFN.scala 500:29]
  wire  _T_628 = sExpX3_13 <= _GEN_15; // @[MulAddRecFN.scala 500:29]
  wire  _T_629 = sExpX3[10] | _T_628; // @[MulAddRecFN.scala 499:35]
  wire  underflowY = inexactY & _T_629; // @[MulAddRecFN.scala 498:22]
  wire  roundMagUp = roundingMode_min & signY | roundingMode_max & ~signY; // @[MulAddRecFN.scala 506:37]
  wire  overflowY_roundMagUp = roundingMode_nearest_even | roundMagUp; // @[MulAddRecFN.scala 507:58]
  wire  mulSpecial = isSpecialA | isSpecialB; // @[MulAddRecFN.scala 511:33]
  wire  addSpecial = mulSpecial | isSpecialC; // @[MulAddRecFN.scala 512:33]
  wire  notSpecial_addZeros = io_fromPreMul_isZeroProd & isZeroC; // @[MulAddRecFN.scala 513:56]
  wire  commonCase = ~addSpecial & ~notSpecial_addZeros; // @[MulAddRecFN.scala 514:35]
  wire  _T_646 = isInfA | isInfB; // @[MulAddRecFN.scala 518:46]
  wire  _T_649 = ~isNaNA & ~isNaNB & (isInfA | isInfB) & isInfC & doSubMags; // @[MulAddRecFN.scala 518:67]
  wire  notSigNaN_invalid = isInfA & isZeroB | isZeroA & isInfB | _T_649; // @[MulAddRecFN.scala 517:52]
  wire  invalid = isSigNaNA | isSigNaNB | isSigNaNC | notSigNaN_invalid; // @[MulAddRecFN.scala 519:55]
  wire  overflow = commonCase & overflowY; // @[MulAddRecFN.scala 520:32]
  wire  underflow = commonCase & underflowY; // @[MulAddRecFN.scala 521:32]
  wire  inexact = overflow | commonCase & inexactY; // @[MulAddRecFN.scala 522:28]
  wire  notSpecial_isZeroOut = notSpecial_addZeros | isZeroY | totalUnderflowY; // @[MulAddRecFN.scala 525:40]
  wire  pegMinFiniteMagOut = commonCase & totalUnderflowY & roundMagUp; // @[MulAddRecFN.scala 526:60]
  wire  pegMaxFiniteMagOut = overflow & ~overflowY_roundMagUp; // @[MulAddRecFN.scala 527:39]
  wire  notNaN_isInfOut = _T_646 | isInfC | overflow & overflowY_roundMagUp; // @[MulAddRecFN.scala 529:36]
  wire  isNaNOut = isNaNA | isNaNB | isNaNC | notSigNaN_invalid; // @[MulAddRecFN.scala 530:47]
  wire  _T_668 = mulSpecial & ~isSpecialC & io_fromPreMul_signProd; // @[MulAddRecFN.scala 534:51]
  wire  _T_669 = _T_264 & io_fromPreMul_opSignC | _T_668; // @[MulAddRecFN.scala 533:78]
  wire  _T_671 = ~mulSpecial; // @[MulAddRecFN.scala 535:10]
  wire  _T_673 = ~mulSpecial & isSpecialC & io_fromPreMul_opSignC; // @[MulAddRecFN.scala 535:51]
  wire  _T_674 = _T_669 | _T_673; // @[MulAddRecFN.scala 534:78]
  wire  _T_679 = _T_671 & notSpecial_addZeros & doSubMags & roundingMode_min; // @[MulAddRecFN.scala 536:59]
  wire  uncommonCaseSignOut = _T_674 | _T_679; // @[MulAddRecFN.scala 535:78]
  wire  signOut = ~isNaNOut & uncommonCaseSignOut | commonCase & signY; // @[MulAddRecFN.scala 538:55]
  wire [8:0] _T_686 = notSpecial_isZeroOut ? 9'h1c0 : 9'h0; // @[MulAddRecFN.scala 541:18]
  wire [8:0] _T_687 = ~_T_686; // @[MulAddRecFN.scala 541:14]
  wire [8:0] _T_688 = expY & _T_687; // @[MulAddRecFN.scala 540:15]
  wire [8:0] _T_692 = pegMinFiniteMagOut ? 9'h194 : 9'h0; // @[MulAddRecFN.scala 545:18]
  wire [8:0] _T_693 = ~_T_692; // @[MulAddRecFN.scala 545:14]
  wire [8:0] _T_694 = _T_688 & _T_693; // @[MulAddRecFN.scala 544:17]
  wire [8:0] _T_697 = pegMaxFiniteMagOut ? 9'h80 : 9'h0; // @[MulAddRecFN.scala 549:18]
  wire [8:0] _T_698 = ~_T_697; // @[MulAddRecFN.scala 549:14]
  wire [8:0] _T_699 = _T_694 & _T_698; // @[MulAddRecFN.scala 548:17]
  wire [8:0] _T_702 = notNaN_isInfOut ? 9'h40 : 9'h0; // @[MulAddRecFN.scala 553:18]
  wire [8:0] _T_703 = ~_T_702; // @[MulAddRecFN.scala 553:14]
  wire [8:0] _T_704 = _T_699 & _T_703; // @[MulAddRecFN.scala 552:17]
  wire [8:0] _T_707 = pegMinFiniteMagOut ? 9'h6b : 9'h0; // @[MulAddRecFN.scala 557:16]
  wire [8:0] _T_708 = _T_704 | _T_707; // @[MulAddRecFN.scala 556:18]
  wire [8:0] _T_711 = pegMaxFiniteMagOut ? 9'h17f : 9'h0; // @[MulAddRecFN.scala 558:16]
  wire [8:0] _T_712 = _T_708 | _T_711; // @[MulAddRecFN.scala 557:74]
  wire [8:0] _T_715 = notNaN_isInfOut ? 9'h180 : 9'h0; // @[MulAddRecFN.scala 562:16]
  wire [8:0] _T_716 = _T_712 | _T_715; // @[MulAddRecFN.scala 561:15]
  wire [8:0] _T_719 = isNaNOut ? 9'h1c0 : 9'h0; // @[MulAddRecFN.scala 566:16]
  wire [8:0] expOut = _T_716 | _T_719; // @[MulAddRecFN.scala 565:15]
  wire [22:0] _T_725 = isNaNOut ? 23'h400000 : 23'h0; // @[MulAddRecFN.scala 569:16]
  wire [22:0] _T_726 = totalUnderflowY & roundMagUp | isNaNOut ? _T_725 : fractY; // @[MulAddRecFN.scala 568:12]
  wire [22:0] _T_730 = pegMaxFiniteMagOut ? 23'h7fffff : 23'h0; // @[Bitwise.scala 71:12]
  wire [22:0] fractOut = _T_726 | _T_730; // @[MulAddRecFN.scala 571:11]
  wire [9:0] _T_731 = {signOut,expOut}; // @[Cat.scala 30:58]
  wire [1:0] _T_734 = {underflow,inexact}; // @[Cat.scala 30:58]
  wire [2:0] _T_736 = {invalid,1'h0,overflow}; // @[Cat.scala 30:58]
  assign io_out = {_T_731,fractOut}; // @[Cat.scala 30:58]
  assign io_exceptionFlags = {_T_736,_T_734}; // @[Cat.scala 30:58]
endmodule
module MulAddRecFN_preMul_1(
  input  [1:0]   io_op,
  input  [64:0]  io_a,
  input  [64:0]  io_b,
  input  [64:0]  io_c,
  input  [1:0]   io_roundingMode,
  output [52:0]  io_mulAddA,
  output [52:0]  io_mulAddB,
  output [105:0] io_mulAddC,
  output [2:0]   io_toPostMul_highExpA,
  output         io_toPostMul_isNaN_isQuietNaNA,
  output [2:0]   io_toPostMul_highExpB,
  output         io_toPostMul_isNaN_isQuietNaNB,
  output         io_toPostMul_signProd,
  output         io_toPostMul_isZeroProd,
  output         io_toPostMul_opSignC,
  output [2:0]   io_toPostMul_highExpC,
  output         io_toPostMul_isNaN_isQuietNaNC,
  output         io_toPostMul_isCDominant,
  output         io_toPostMul_CAlignDist_0,
  output [7:0]   io_toPostMul_CAlignDist,
  output         io_toPostMul_bit0AlignedNegSigC,
  output [54:0]  io_toPostMul_highAlignedNegSigC,
  output [13:0]  io_toPostMul_sExpSum,
  output [1:0]   io_toPostMul_roundingMode
);
  wire  signA = io_a[64]; // @[MulAddRecFN.scala 102:22]
  wire [11:0] expA = io_a[63:52]; // @[MulAddRecFN.scala 103:22]
  wire [51:0] fractA = io_a[51:0]; // @[MulAddRecFN.scala 104:22]
  wire  isZeroA = expA[11:9] == 3'h0; // @[MulAddRecFN.scala 105:49]
  wire  _T_55 = ~isZeroA; // @[MulAddRecFN.scala 106:20]
  wire  signB = io_b[64]; // @[MulAddRecFN.scala 108:22]
  wire [11:0] expB = io_b[63:52]; // @[MulAddRecFN.scala 109:22]
  wire [51:0] fractB = io_b[51:0]; // @[MulAddRecFN.scala 110:22]
  wire  isZeroB = expB[11:9] == 3'h0; // @[MulAddRecFN.scala 111:49]
  wire  _T_59 = ~isZeroB; // @[MulAddRecFN.scala 112:20]
  wire  opSignC = io_c[64] ^ io_op[0]; // @[MulAddRecFN.scala 114:45]
  wire [11:0] expC = io_c[63:52]; // @[MulAddRecFN.scala 115:22]
  wire [51:0] fractC = io_c[51:0]; // @[MulAddRecFN.scala 116:22]
  wire  isZeroC = expC[11:9] == 3'h0; // @[MulAddRecFN.scala 117:49]
  wire  _T_65 = ~isZeroC; // @[MulAddRecFN.scala 118:20]
  wire [52:0] sigC = {_T_65,fractC}; // @[Cat.scala 30:58]
  wire  signProd = signA ^ signB ^ io_op[1]; // @[MulAddRecFN.scala 122:34]
  wire  isZeroProd = isZeroA | isZeroB; // @[MulAddRecFN.scala 123:30]
  wire  _T_70 = ~expB[11]; // @[MulAddRecFN.scala 125:28]
  wire [2:0] _T_74 = _T_70 ? 3'h7 : 3'h0; // @[Bitwise.scala 71:12]
  wire [13:0] _T_76 = {_T_74,expB[10:0]}; // @[Cat.scala 30:58]
  wire [13:0] _GEN_0 = {{2'd0}, expA}; // @[MulAddRecFN.scala 125:14]
  wire [13:0] _T_78 = _GEN_0 + _T_76; // @[MulAddRecFN.scala 125:14]
  wire [13:0] sExpAlignedProd = _T_78 + 14'h38; // @[MulAddRecFN.scala 125:70]
  wire  doSubMags = signProd ^ opSignC; // @[MulAddRecFN.scala 130:30]
  wire [13:0] _GEN_1 = {{2'd0}, expC}; // @[MulAddRecFN.scala 132:42]
  wire [13:0] sNatCAlignDist = sExpAlignedProd - _GEN_1; // @[MulAddRecFN.scala 132:42]
  wire  CAlignDist_floor = isZeroProd | sNatCAlignDist[13]; // @[MulAddRecFN.scala 133:39]
  wire  _T_91 = sNatCAlignDist[12:0] < 13'h36; // @[MulAddRecFN.scala 139:51]
  wire  _T_92 = CAlignDist_floor | _T_91; // @[MulAddRecFN.scala 138:31]
  wire [7:0] _T_99 = sNatCAlignDist[12:0] < 13'ha1 ? sNatCAlignDist[7:0] : 8'ha1; // @[MulAddRecFN.scala 143:16]
  wire [7:0] CAlignDist = CAlignDist_floor ? 8'h0 : _T_99; // @[MulAddRecFN.scala 141:12]
  wire [64:0] _T_106 = 65'sh10000000000000000 >>> CAlignDist[5:0]; // @[primitives.scala 68:52]
  wire [31:0] _T_114 = {{16'd0}, _T_106[62:47]}; // @[Bitwise.scala 102:31]
  wire [31:0] _T_116 = {_T_106[46:31], 16'h0}; // @[Bitwise.scala 102:65]
  wire [31:0] _T_118 = _T_116 & 32'hffff0000; // @[Bitwise.scala 102:75]
  wire [31:0] _T_119 = _T_114 | _T_118; // @[Bitwise.scala 102:39]
  wire [31:0] _GEN_2 = {{8'd0}, _T_119[31:8]}; // @[Bitwise.scala 102:31]
  wire [31:0] _T_124 = _GEN_2 & 32'hff00ff; // @[Bitwise.scala 102:31]
  wire [31:0] _T_126 = {_T_119[23:0], 8'h0}; // @[Bitwise.scala 102:65]
  wire [31:0] _T_128 = _T_126 & 32'hff00ff00; // @[Bitwise.scala 102:75]
  wire [31:0] _T_129 = _T_124 | _T_128; // @[Bitwise.scala 102:39]
  wire [31:0] _GEN_3 = {{4'd0}, _T_129[31:4]}; // @[Bitwise.scala 102:31]
  wire [31:0] _T_134 = _GEN_3 & 32'hf0f0f0f; // @[Bitwise.scala 102:31]
  wire [31:0] _T_136 = {_T_129[27:0], 4'h0}; // @[Bitwise.scala 102:65]
  wire [31:0] _T_138 = _T_136 & 32'hf0f0f0f0; // @[Bitwise.scala 102:75]
  wire [31:0] _T_139 = _T_134 | _T_138; // @[Bitwise.scala 102:39]
  wire [31:0] _GEN_4 = {{2'd0}, _T_139[31:2]}; // @[Bitwise.scala 102:31]
  wire [31:0] _T_144 = _GEN_4 & 32'h33333333; // @[Bitwise.scala 102:31]
  wire [31:0] _T_146 = {_T_139[29:0], 2'h0}; // @[Bitwise.scala 102:65]
  wire [31:0] _T_148 = _T_146 & 32'hcccccccc; // @[Bitwise.scala 102:75]
  wire [31:0] _T_149 = _T_144 | _T_148; // @[Bitwise.scala 102:39]
  wire [31:0] _GEN_5 = {{1'd0}, _T_149[31:1]}; // @[Bitwise.scala 102:31]
  wire [31:0] _T_154 = _GEN_5 & 32'h55555555; // @[Bitwise.scala 102:31]
  wire [31:0] _T_156 = {_T_149[30:0], 1'h0}; // @[Bitwise.scala 102:65]
  wire [31:0] _T_158 = _T_156 & 32'haaaaaaaa; // @[Bitwise.scala 102:75]
  wire [31:0] _T_159 = _T_154 | _T_158; // @[Bitwise.scala 102:39]
  wire [32:0] _T_161 = {_T_159,_T_106[63]}; // @[Cat.scala 30:58]
  wire [32:0] _T_162 = ~_T_161; // @[primitives.scala 65:36]
  wire [32:0] _T_163 = CAlignDist[6] ? 33'h0 : _T_162; // @[primitives.scala 65:21]
  wire [32:0] _T_164 = ~_T_163; // @[primitives.scala 65:17]
  wire [52:0] _T_166 = {_T_164,20'hfffff}; // @[Cat.scala 30:58]
  wire [15:0] _T_178 = {{8'd0}, _T_106[15:8]}; // @[Bitwise.scala 102:31]
  wire [15:0] _T_180 = {_T_106[7:0], 8'h0}; // @[Bitwise.scala 102:65]
  wire [15:0] _T_182 = _T_180 & 16'hff00; // @[Bitwise.scala 102:75]
  wire [15:0] _T_183 = _T_178 | _T_182; // @[Bitwise.scala 102:39]
  wire [15:0] _GEN_6 = {{4'd0}, _T_183[15:4]}; // @[Bitwise.scala 102:31]
  wire [15:0] _T_188 = _GEN_6 & 16'hf0f; // @[Bitwise.scala 102:31]
  wire [15:0] _T_190 = {_T_183[11:0], 4'h0}; // @[Bitwise.scala 102:65]
  wire [15:0] _T_192 = _T_190 & 16'hf0f0; // @[Bitwise.scala 102:75]
  wire [15:0] _T_193 = _T_188 | _T_192; // @[Bitwise.scala 102:39]
  wire [15:0] _GEN_7 = {{2'd0}, _T_193[15:2]}; // @[Bitwise.scala 102:31]
  wire [15:0] _T_198 = _GEN_7 & 16'h3333; // @[Bitwise.scala 102:31]
  wire [15:0] _T_200 = {_T_193[13:0], 2'h0}; // @[Bitwise.scala 102:65]
  wire [15:0] _T_202 = _T_200 & 16'hcccc; // @[Bitwise.scala 102:75]
  wire [15:0] _T_203 = _T_198 | _T_202; // @[Bitwise.scala 102:39]
  wire [15:0] _GEN_8 = {{1'd0}, _T_203[15:1]}; // @[Bitwise.scala 102:31]
  wire [15:0] _T_208 = _GEN_8 & 16'h5555; // @[Bitwise.scala 102:31]
  wire [15:0] _T_210 = {_T_203[14:0], 1'h0}; // @[Bitwise.scala 102:65]
  wire [15:0] _T_212 = _T_210 & 16'haaaa; // @[Bitwise.scala 102:75]
  wire [15:0] _T_213 = _T_208 | _T_212; // @[Bitwise.scala 102:39]
  wire [19:0] _T_224 = {_T_213,_T_106[16],_T_106[17],_T_106[18],_T_106[19]}; // @[Cat.scala 30:58]
  wire [19:0] _T_226 = CAlignDist[6] ? _T_224 : 20'h0; // @[primitives.scala 59:20]
  wire [52:0] CExtraMask = CAlignDist[7] ? _T_166 : {{33'd0}, _T_226}; // @[primitives.scala 61:20]
  wire [52:0] _T_227 = ~sigC; // @[MulAddRecFN.scala 151:34]
  wire [52:0] negSigC = doSubMags ? _T_227 : sigC; // @[MulAddRecFN.scala 151:22]
  wire [107:0] _T_231 = doSubMags ? 108'hfffffffffffffffffffffffffff : 108'h0; // @[Bitwise.scala 71:12]
  wire [161:0] _T_234 = {doSubMags,negSigC,_T_231}; // @[MulAddRecFN.scala 154:64]
  wire [52:0] _T_236 = sigC & CExtraMask; // @[MulAddRecFN.scala 156:19]
  wire  _T_239 = _T_236 != 53'h0 ^ doSubMags; // @[MulAddRecFN.scala 156:37]
  wire [161:0] _T_240 = $signed(_T_234) >>> CAlignDist; // @[Cat.scala 30:58]
  wire [162:0] _T_241 = {_T_240,_T_239}; // @[Cat.scala 30:58]
  wire [161:0] alignedNegSigC = _T_241[161:0]; // @[MulAddRecFN.scala 157:10]
  assign io_mulAddA = {_T_55,fractA}; // @[Cat.scala 30:58]
  assign io_mulAddB = {_T_59,fractB}; // @[Cat.scala 30:58]
  assign io_mulAddC = alignedNegSigC[106:1]; // @[MulAddRecFN.scala 161:33]
  assign io_toPostMul_highExpA = expA[11:9]; // @[MulAddRecFN.scala 163:44]
  assign io_toPostMul_isNaN_isQuietNaNA = fractA[51]; // @[MulAddRecFN.scala 164:46]
  assign io_toPostMul_highExpB = expB[11:9]; // @[MulAddRecFN.scala 165:44]
  assign io_toPostMul_isNaN_isQuietNaNB = fractB[51]; // @[MulAddRecFN.scala 166:46]
  assign io_toPostMul_signProd = signA ^ signB ^ io_op[1]; // @[MulAddRecFN.scala 122:34]
  assign io_toPostMul_isZeroProd = isZeroA | isZeroB; // @[MulAddRecFN.scala 123:30]
  assign io_toPostMul_opSignC = io_c[64] ^ io_op[0]; // @[MulAddRecFN.scala 114:45]
  assign io_toPostMul_highExpC = expC[11:9]; // @[MulAddRecFN.scala 170:44]
  assign io_toPostMul_isNaN_isQuietNaNC = fractC[51]; // @[MulAddRecFN.scala 171:46]
  assign io_toPostMul_isCDominant = _T_65 & _T_92; // @[MulAddRecFN.scala 137:19]
  assign io_toPostMul_CAlignDist_0 = CAlignDist_floor | sNatCAlignDist[12:0] == 13'h0; // @[MulAddRecFN.scala 135:26]
  assign io_toPostMul_CAlignDist = CAlignDist_floor ? 8'h0 : _T_99; // @[MulAddRecFN.scala 141:12]
  assign io_toPostMul_bit0AlignedNegSigC = alignedNegSigC[0]; // @[MulAddRecFN.scala 175:54]
  assign io_toPostMul_highAlignedNegSigC = alignedNegSigC[161:107]; // @[MulAddRecFN.scala 177:23]
  assign io_toPostMul_sExpSum = CAlignDist_floor ? {{2'd0}, expC} : sExpAlignedProd; // @[MulAddRecFN.scala 148:22]
  assign io_toPostMul_roundingMode = io_roundingMode; // @[MulAddRecFN.scala 179:37]
endmodule
module MulAddRecFN_postMul_1(
  input  [2:0]   io_fromPreMul_highExpA,
  input          io_fromPreMul_isNaN_isQuietNaNA,
  input  [2:0]   io_fromPreMul_highExpB,
  input          io_fromPreMul_isNaN_isQuietNaNB,
  input          io_fromPreMul_signProd,
  input          io_fromPreMul_isZeroProd,
  input          io_fromPreMul_opSignC,
  input  [2:0]   io_fromPreMul_highExpC,
  input          io_fromPreMul_isNaN_isQuietNaNC,
  input          io_fromPreMul_isCDominant,
  input          io_fromPreMul_CAlignDist_0,
  input  [7:0]   io_fromPreMul_CAlignDist,
  input          io_fromPreMul_bit0AlignedNegSigC,
  input  [54:0]  io_fromPreMul_highAlignedNegSigC,
  input  [13:0]  io_fromPreMul_sExpSum,
  input  [1:0]   io_fromPreMul_roundingMode,
  input  [106:0] io_mulAddResult,
  output [64:0]  io_out,
  output [4:0]   io_exceptionFlags
);
  wire  isZeroA = io_fromPreMul_highExpA == 3'h0; // @[MulAddRecFN.scala 207:46]
  wire  isSpecialA = io_fromPreMul_highExpA[2:1] == 2'h3; // @[MulAddRecFN.scala 208:52]
  wire  isInfA = isSpecialA & ~io_fromPreMul_highExpA[0]; // @[MulAddRecFN.scala 209:29]
  wire  isNaNA = isSpecialA & io_fromPreMul_highExpA[0]; // @[MulAddRecFN.scala 210:29]
  wire  isSigNaNA = isNaNA & ~io_fromPreMul_isNaN_isQuietNaNA; // @[MulAddRecFN.scala 211:28]
  wire  isZeroB = io_fromPreMul_highExpB == 3'h0; // @[MulAddRecFN.scala 213:46]
  wire  isSpecialB = io_fromPreMul_highExpB[2:1] == 2'h3; // @[MulAddRecFN.scala 214:52]
  wire  isInfB = isSpecialB & ~io_fromPreMul_highExpB[0]; // @[MulAddRecFN.scala 215:29]
  wire  isNaNB = isSpecialB & io_fromPreMul_highExpB[0]; // @[MulAddRecFN.scala 216:29]
  wire  isSigNaNB = isNaNB & ~io_fromPreMul_isNaN_isQuietNaNB; // @[MulAddRecFN.scala 217:28]
  wire  isZeroC = io_fromPreMul_highExpC == 3'h0; // @[MulAddRecFN.scala 219:46]
  wire  isSpecialC = io_fromPreMul_highExpC[2:1] == 2'h3; // @[MulAddRecFN.scala 220:52]
  wire  isInfC = isSpecialC & ~io_fromPreMul_highExpC[0]; // @[MulAddRecFN.scala 221:29]
  wire  isNaNC = isSpecialC & io_fromPreMul_highExpC[0]; // @[MulAddRecFN.scala 222:29]
  wire  isSigNaNC = isNaNC & ~io_fromPreMul_isNaN_isQuietNaNC; // @[MulAddRecFN.scala 223:28]
  wire  roundingMode_nearest_even = io_fromPreMul_roundingMode == 2'h0; // @[MulAddRecFN.scala 226:37]
  wire  roundingMode_min = io_fromPreMul_roundingMode == 2'h2; // @[MulAddRecFN.scala 228:59]
  wire  roundingMode_max = io_fromPreMul_roundingMode == 2'h3; // @[MulAddRecFN.scala 229:59]
  wire  doSubMags = io_fromPreMul_signProd ^ io_fromPreMul_opSignC; // @[MulAddRecFN.scala 232:44]
  wire [54:0] _T_74 = io_fromPreMul_highAlignedNegSigC + 55'h1; // @[MulAddRecFN.scala 238:50]
  wire [54:0] _T_75 = io_mulAddResult[106] ? _T_74 : io_fromPreMul_highAlignedNegSigC; // @[MulAddRecFN.scala 237:16]
  wire [161:0] sigSum = {_T_75,io_mulAddResult[105:0],io_fromPreMul_bit0AlignedNegSigC}; // @[Cat.scala 30:58]
  wire [108:0] _T_82 = {sigSum[108:1], 1'h0}; // @[MulAddRecFN.scala 191:41]
  wire [108:0] _GEN_0 = {{1'd0}, sigSum[108:1]}; // @[MulAddRecFN.scala 191:32]
  wire [108:0] _T_83 = _GEN_0 ^ _T_82; // @[MulAddRecFN.scala 191:32]
  wire  _T_89 = _T_83[107:64] != 44'h0; // @[CircuitMath.scala 37:22]
  wire  _T_93 = _T_83[107:96] != 12'h0; // @[CircuitMath.scala 37:22]
  wire  _T_97 = _T_83[107:104] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_103 = _T_83[106] ? 2'h2 : {{1'd0}, _T_83[105]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_104 = _T_83[107] ? 2'h3 : _T_103; // @[CircuitMath.scala 32:10]
  wire  _T_108 = _T_83[103:100] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_114 = _T_83[102] ? 2'h2 : {{1'd0}, _T_83[101]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_115 = _T_83[103] ? 2'h3 : _T_114; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_121 = _T_83[98] ? 2'h2 : {{1'd0}, _T_83[97]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_122 = _T_83[99] ? 2'h3 : _T_121; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_123 = _T_108 ? _T_115 : _T_122; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_124 = {_T_108,_T_123}; // @[Cat.scala 30:58]
  wire [2:0] _T_125 = _T_97 ? {{1'd0}, _T_104} : _T_124; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_126 = {_T_97,_T_125}; // @[Cat.scala 30:58]
  wire  _T_130 = _T_83[95:80] != 16'h0; // @[CircuitMath.scala 37:22]
  wire  _T_134 = _T_83[95:88] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_138 = _T_83[95:92] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_144 = _T_83[94] ? 2'h2 : {{1'd0}, _T_83[93]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_145 = _T_83[95] ? 2'h3 : _T_144; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_151 = _T_83[90] ? 2'h2 : {{1'd0}, _T_83[89]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_152 = _T_83[91] ? 2'h3 : _T_151; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_153 = _T_138 ? _T_145 : _T_152; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_154 = {_T_138,_T_153}; // @[Cat.scala 30:58]
  wire  _T_158 = _T_83[87:84] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_164 = _T_83[86] ? 2'h2 : {{1'd0}, _T_83[85]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_165 = _T_83[87] ? 2'h3 : _T_164; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_171 = _T_83[82] ? 2'h2 : {{1'd0}, _T_83[81]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_172 = _T_83[83] ? 2'h3 : _T_171; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_173 = _T_158 ? _T_165 : _T_172; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_174 = {_T_158,_T_173}; // @[Cat.scala 30:58]
  wire [2:0] _T_175 = _T_134 ? _T_154 : _T_174; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_176 = {_T_134,_T_175}; // @[Cat.scala 30:58]
  wire  _T_180 = _T_83[79:72] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_184 = _T_83[79:76] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_190 = _T_83[78] ? 2'h2 : {{1'd0}, _T_83[77]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_191 = _T_83[79] ? 2'h3 : _T_190; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_197 = _T_83[74] ? 2'h2 : {{1'd0}, _T_83[73]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_198 = _T_83[75] ? 2'h3 : _T_197; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_199 = _T_184 ? _T_191 : _T_198; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_200 = {_T_184,_T_199}; // @[Cat.scala 30:58]
  wire  _T_204 = _T_83[71:68] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_210 = _T_83[70] ? 2'h2 : {{1'd0}, _T_83[69]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_211 = _T_83[71] ? 2'h3 : _T_210; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_217 = _T_83[66] ? 2'h2 : {{1'd0}, _T_83[65]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_218 = _T_83[67] ? 2'h3 : _T_217; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_219 = _T_204 ? _T_211 : _T_218; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_220 = {_T_204,_T_219}; // @[Cat.scala 30:58]
  wire [2:0] _T_221 = _T_180 ? _T_200 : _T_220; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_222 = {_T_180,_T_221}; // @[Cat.scala 30:58]
  wire [3:0] _T_223 = _T_130 ? _T_176 : _T_222; // @[CircuitMath.scala 38:21]
  wire [4:0] _T_224 = {_T_130,_T_223}; // @[Cat.scala 30:58]
  wire [4:0] _T_225 = _T_93 ? {{1'd0}, _T_126} : _T_224; // @[CircuitMath.scala 38:21]
  wire [5:0] _T_226 = {_T_93,_T_225}; // @[Cat.scala 30:58]
  wire  _T_230 = _T_83[63:32] != 32'h0; // @[CircuitMath.scala 37:22]
  wire  _T_234 = _T_83[63:48] != 16'h0; // @[CircuitMath.scala 37:22]
  wire  _T_238 = _T_83[63:56] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_242 = _T_83[63:60] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_248 = _T_83[62] ? 2'h2 : {{1'd0}, _T_83[61]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_249 = _T_83[63] ? 2'h3 : _T_248; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_255 = _T_83[58] ? 2'h2 : {{1'd0}, _T_83[57]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_256 = _T_83[59] ? 2'h3 : _T_255; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_257 = _T_242 ? _T_249 : _T_256; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_258 = {_T_242,_T_257}; // @[Cat.scala 30:58]
  wire  _T_262 = _T_83[55:52] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_268 = _T_83[54] ? 2'h2 : {{1'd0}, _T_83[53]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_269 = _T_83[55] ? 2'h3 : _T_268; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_275 = _T_83[50] ? 2'h2 : {{1'd0}, _T_83[49]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_276 = _T_83[51] ? 2'h3 : _T_275; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_277 = _T_262 ? _T_269 : _T_276; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_278 = {_T_262,_T_277}; // @[Cat.scala 30:58]
  wire [2:0] _T_279 = _T_238 ? _T_258 : _T_278; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_280 = {_T_238,_T_279}; // @[Cat.scala 30:58]
  wire  _T_284 = _T_83[47:40] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_288 = _T_83[47:44] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_294 = _T_83[46] ? 2'h2 : {{1'd0}, _T_83[45]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_295 = _T_83[47] ? 2'h3 : _T_294; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_301 = _T_83[42] ? 2'h2 : {{1'd0}, _T_83[41]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_302 = _T_83[43] ? 2'h3 : _T_301; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_303 = _T_288 ? _T_295 : _T_302; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_304 = {_T_288,_T_303}; // @[Cat.scala 30:58]
  wire  _T_308 = _T_83[39:36] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_314 = _T_83[38] ? 2'h2 : {{1'd0}, _T_83[37]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_315 = _T_83[39] ? 2'h3 : _T_314; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_321 = _T_83[34] ? 2'h2 : {{1'd0}, _T_83[33]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_322 = _T_83[35] ? 2'h3 : _T_321; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_323 = _T_308 ? _T_315 : _T_322; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_324 = {_T_308,_T_323}; // @[Cat.scala 30:58]
  wire [2:0] _T_325 = _T_284 ? _T_304 : _T_324; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_326 = {_T_284,_T_325}; // @[Cat.scala 30:58]
  wire [3:0] _T_327 = _T_234 ? _T_280 : _T_326; // @[CircuitMath.scala 38:21]
  wire [4:0] _T_328 = {_T_234,_T_327}; // @[Cat.scala 30:58]
  wire  _T_332 = _T_83[31:16] != 16'h0; // @[CircuitMath.scala 37:22]
  wire  _T_336 = _T_83[31:24] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_340 = _T_83[31:28] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_346 = _T_83[30] ? 2'h2 : {{1'd0}, _T_83[29]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_347 = _T_83[31] ? 2'h3 : _T_346; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_353 = _T_83[26] ? 2'h2 : {{1'd0}, _T_83[25]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_354 = _T_83[27] ? 2'h3 : _T_353; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_355 = _T_340 ? _T_347 : _T_354; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_356 = {_T_340,_T_355}; // @[Cat.scala 30:58]
  wire  _T_360 = _T_83[23:20] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_366 = _T_83[22] ? 2'h2 : {{1'd0}, _T_83[21]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_367 = _T_83[23] ? 2'h3 : _T_366; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_373 = _T_83[18] ? 2'h2 : {{1'd0}, _T_83[17]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_374 = _T_83[19] ? 2'h3 : _T_373; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_375 = _T_360 ? _T_367 : _T_374; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_376 = {_T_360,_T_375}; // @[Cat.scala 30:58]
  wire [2:0] _T_377 = _T_336 ? _T_356 : _T_376; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_378 = {_T_336,_T_377}; // @[Cat.scala 30:58]
  wire  _T_382 = _T_83[15:8] != 8'h0; // @[CircuitMath.scala 37:22]
  wire  _T_386 = _T_83[15:12] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_392 = _T_83[14] ? 2'h2 : {{1'd0}, _T_83[13]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_393 = _T_83[15] ? 2'h3 : _T_392; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_399 = _T_83[10] ? 2'h2 : {{1'd0}, _T_83[9]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_400 = _T_83[11] ? 2'h3 : _T_399; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_401 = _T_386 ? _T_393 : _T_400; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_402 = {_T_386,_T_401}; // @[Cat.scala 30:58]
  wire  _T_406 = _T_83[7:4] != 4'h0; // @[CircuitMath.scala 37:22]
  wire [1:0] _T_412 = _T_83[6] ? 2'h2 : {{1'd0}, _T_83[5]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_413 = _T_83[7] ? 2'h3 : _T_412; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_419 = _T_83[2] ? 2'h2 : {{1'd0}, _T_83[1]}; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_420 = _T_83[3] ? 2'h3 : _T_419; // @[CircuitMath.scala 32:10]
  wire [1:0] _T_421 = _T_406 ? _T_413 : _T_420; // @[CircuitMath.scala 38:21]
  wire [2:0] _T_422 = {_T_406,_T_421}; // @[Cat.scala 30:58]
  wire [2:0] _T_423 = _T_382 ? _T_402 : _T_422; // @[CircuitMath.scala 38:21]
  wire [3:0] _T_424 = {_T_382,_T_423}; // @[Cat.scala 30:58]
  wire [3:0] _T_425 = _T_332 ? _T_378 : _T_424; // @[CircuitMath.scala 38:21]
  wire [4:0] _T_426 = {_T_332,_T_425}; // @[Cat.scala 30:58]
  wire [4:0] _T_427 = _T_230 ? _T_328 : _T_426; // @[CircuitMath.scala 38:21]
  wire [5:0] _T_428 = {_T_230,_T_427}; // @[Cat.scala 30:58]
  wire [5:0] _T_429 = _T_89 ? _T_226 : _T_428; // @[CircuitMath.scala 38:21]
  wire [6:0] _T_430 = {_T_89,_T_429}; // @[Cat.scala 30:58]
  wire [7:0] _GEN_1 = {{1'd0}, _T_430}; // @[primitives.scala 79:25]
  wire [7:0] estNormNeg_dist = 8'ha0 - _GEN_1; // @[primitives.scala 79:25]
  wire  _T_435 = sigSum[75:44] != 32'h0; // @[MulAddRecFN.scala 254:15]
  wire  _T_438 = sigSum[43:0] != 44'h0; // @[MulAddRecFN.scala 255:57]
  wire [1:0] firstReduceSigSum = {_T_435,_T_438}; // @[Cat.scala 30:58]
  wire [161:0] complSigSum = ~sigSum; // @[MulAddRecFN.scala 257:23]
  wire  _T_441 = complSigSum[75:44] != 32'h0; // @[MulAddRecFN.scala 261:15]
  wire  _T_444 = complSigSum[43:0] != 44'h0; // @[MulAddRecFN.scala 262:62]
  wire [1:0] firstReduceComplSigSum = {_T_441,_T_444}; // @[Cat.scala 30:58]
  wire [7:0] _T_449 = io_fromPreMul_CAlignDist - 8'h1; // @[MulAddRecFN.scala 268:39]
  wire [7:0] CDom_estNormDist = io_fromPreMul_CAlignDist_0 | doSubMags ? io_fromPreMul_CAlignDist : {{2'd0}, _T_449[5:0]
    }; // @[MulAddRecFN.scala 266:12]
  wire  _T_452 = ~doSubMags; // @[MulAddRecFN.scala 271:13]
  wire  _T_455 = ~CDom_estNormDist[5]; // @[MulAddRecFN.scala 271:28]
  wire  _T_459 = firstReduceSigSum != 2'h0; // @[MulAddRecFN.scala 273:35]
  wire [86:0] _T_460 = {sigSum[161:76],_T_459}; // @[Cat.scala 30:58]
  wire [86:0] _T_462 = ~doSubMags & ~CDom_estNormDist[5] ? _T_460 : 87'h0; // @[MulAddRecFN.scala 271:12]
  wire [86:0] _T_469 = {sigSum[129:44],firstReduceSigSum[0]}; // @[Cat.scala 30:58]
  wire [86:0] _T_471 = _T_452 & CDom_estNormDist[5] ? _T_469 : 87'h0; // @[MulAddRecFN.scala 277:12]
  wire [86:0] _T_472 = _T_462 | _T_471; // @[MulAddRecFN.scala 276:11]
  wire  _T_479 = firstReduceComplSigSum != 2'h0; // @[MulAddRecFN.scala 288:40]
  wire [86:0] _T_480 = {complSigSum[161:76],_T_479}; // @[Cat.scala 30:58]
  wire [86:0] _T_482 = doSubMags & _T_455 ? _T_480 : 87'h0; // @[MulAddRecFN.scala 286:12]
  wire [86:0] _T_483 = _T_472 | _T_482; // @[MulAddRecFN.scala 285:11]
  wire [86:0] _T_488 = {complSigSum[129:44],firstReduceComplSigSum[0]}; // @[Cat.scala 30:58]
  wire [86:0] _T_490 = doSubMags & CDom_estNormDist[5] ? _T_488 : 87'h0; // @[MulAddRecFN.scala 292:12]
  wire [86:0] CDom_firstNormAbsSigSum = _T_483 | _T_490; // @[MulAddRecFN.scala 291:11]
  wire  _T_494 = ~firstReduceComplSigSum[0]; // @[MulAddRecFN.scala 310:21]
  wire  _T_496 = doSubMags ? _T_494 : firstReduceSigSum[0]; // @[MulAddRecFN.scala 309:20]
  wire [65:0] _T_497 = {sigSum[108:44],_T_496}; // @[Cat.scala 30:58]
  wire [85:0] _T_504 = doSubMags ? 86'h3fffffffffffffffffffff : 86'h0; // @[Bitwise.scala 71:12]
  wire [86:0] _T_505 = {sigSum[1],_T_504}; // @[Cat.scala 30:58]
  wire [86:0] _T_506 = estNormNeg_dist[4] ? {{21'd0}, _T_497} : _T_505; // @[MulAddRecFN.scala 316:21]
  wire  _T_510 = complSigSum[11:1] == 11'h0; // @[MulAddRecFN.scala 329:77]
  wire  _T_513 = sigSum[11:1] != 11'h0; // @[MulAddRecFN.scala 331:72]
  wire  _T_514 = doSubMags ? _T_510 : _T_513; // @[MulAddRecFN.scala 328:26]
  wire [86:0] _T_515 = {sigSum[97:12],_T_514}; // @[Cat.scala 30:58]
  wire [21:0] _T_522 = doSubMags ? 22'h3fffff : 22'h0; // @[Bitwise.scala 71:12]
  wire [86:0] _T_523 = {sigSum[65:1],_T_522}; // @[Cat.scala 30:58]
  wire [86:0] _T_524 = estNormNeg_dist[5] ? _T_523 : _T_515; // @[MulAddRecFN.scala 339:17]
  wire [53:0] _T_530 = doSubMags ? 54'h3fffffffffffff : 54'h0; // @[Bitwise.scala 71:12]
  wire [86:0] _T_531 = {sigSum[33:1],_T_530}; // @[Cat.scala 30:58]
  wire [86:0] _T_532 = estNormNeg_dist[5] ? _T_506 : _T_531; // @[MulAddRecFN.scala 345:17]
  wire [86:0] notCDom_pos_firstNormAbsSigSum = estNormNeg_dist[6] ? _T_524 : _T_532; // @[MulAddRecFN.scala 338:12]
  wire [64:0] _T_535 = {complSigSum[107:44],firstReduceComplSigSum[0]}; // @[Cat.scala 30:58]
  wire [87:0] _T_539 = {complSigSum[2:1], 86'h0}; // @[MulAddRecFN.scala 367:68]
  wire [87:0] _T_540 = estNormNeg_dist[4] ? {{23'd0}, _T_535} : _T_539; // @[MulAddRecFN.scala 365:21]
  wire  _T_544 = complSigSum[11:1] != 11'h0; // @[MulAddRecFN.scala 376:71]
  wire [87:0] _T_545 = {complSigSum[98:12],_T_544}; // @[Cat.scala 30:58]
  wire [87:0] _T_549 = {complSigSum[66:1], 22'h0}; // @[MulAddRecFN.scala 381:64]
  wire [87:0] _T_550 = estNormNeg_dist[5] ? _T_549 : _T_545; // @[MulAddRecFN.scala 380:17]
  wire [87:0] _T_553 = {complSigSum[34:1], 54'h0}; // @[MulAddRecFN.scala 387:64]
  wire [87:0] _T_554 = estNormNeg_dist[5] ? _T_540 : _T_553; // @[MulAddRecFN.scala 385:17]
  wire [87:0] notCDom_neg_cFirstNormAbsSigSum = estNormNeg_dist[6] ? _T_550 : _T_554; // @[MulAddRecFN.scala 379:12]
  wire  notCDom_signSigSum = sigSum[109]; // @[MulAddRecFN.scala 392:36]
  wire  _T_557 = doSubMags & ~isZeroC; // @[MulAddRecFN.scala 395:23]
  wire  doNegSignSum = io_fromPreMul_isCDominant ? _T_557 : notCDom_signSigSum; // @[MulAddRecFN.scala 394:12]
  wire [7:0] estNormDist = io_fromPreMul_isCDominant ? CDom_estNormDist : estNormNeg_dist; // @[MulAddRecFN.scala 399:12]
  wire [87:0] _T_559 = io_fromPreMul_isCDominant ? {{1'd0}, CDom_firstNormAbsSigSum} : notCDom_neg_cFirstNormAbsSigSum; // @[MulAddRecFN.scala 408:16]
  wire [86:0] _T_560 = io_fromPreMul_isCDominant ? CDom_firstNormAbsSigSum : notCDom_pos_firstNormAbsSigSum; // @[MulAddRecFN.scala 412:16]
  wire [87:0] cFirstNormAbsSigSum = notCDom_signSigSum ? _T_559 : {{1'd0}, _T_560}; // @[MulAddRecFN.scala 407:12]
  wire  doIncrSig = ~io_fromPreMul_isCDominant & ~notCDom_signSigSum & doSubMags; // @[MulAddRecFN.scala 418:61]
  wire [4:0] estNormDist_5 = estNormDist[4:0]; // @[MulAddRecFN.scala 419:36]
  wire [4:0] normTo2ShiftDist = ~estNormDist_5; // @[MulAddRecFN.scala 420:28]
  wire [32:0] _T_567 = 33'sh100000000 >>> normTo2ShiftDist; // @[primitives.scala 68:52]
  wire [15:0] _T_575 = {{8'd0}, _T_567[16:9]}; // @[Bitwise.scala 102:31]
  wire [15:0] _T_577 = {_T_567[8:1], 8'h0}; // @[Bitwise.scala 102:65]
  wire [15:0] _T_579 = _T_577 & 16'hff00; // @[Bitwise.scala 102:75]
  wire [15:0] _T_580 = _T_575 | _T_579; // @[Bitwise.scala 102:39]
  wire [15:0] _GEN_2 = {{4'd0}, _T_580[15:4]}; // @[Bitwise.scala 102:31]
  wire [15:0] _T_585 = _GEN_2 & 16'hf0f; // @[Bitwise.scala 102:31]
  wire [15:0] _T_587 = {_T_580[11:0], 4'h0}; // @[Bitwise.scala 102:65]
  wire [15:0] _T_589 = _T_587 & 16'hf0f0; // @[Bitwise.scala 102:75]
  wire [15:0] _T_590 = _T_585 | _T_589; // @[Bitwise.scala 102:39]
  wire [15:0] _GEN_3 = {{2'd0}, _T_590[15:2]}; // @[Bitwise.scala 102:31]
  wire [15:0] _T_595 = _GEN_3 & 16'h3333; // @[Bitwise.scala 102:31]
  wire [15:0] _T_597 = {_T_590[13:0], 2'h0}; // @[Bitwise.scala 102:65]
  wire [15:0] _T_599 = _T_597 & 16'hcccc; // @[Bitwise.scala 102:75]
  wire [15:0] _T_600 = _T_595 | _T_599; // @[Bitwise.scala 102:39]
  wire [15:0] _GEN_4 = {{1'd0}, _T_600[15:1]}; // @[Bitwise.scala 102:31]
  wire [15:0] _T_605 = _GEN_4 & 16'h5555; // @[Bitwise.scala 102:31]
  wire [15:0] _T_607 = {_T_600[14:0], 1'h0}; // @[Bitwise.scala 102:65]
  wire [15:0] _T_609 = _T_607 & 16'haaaa; // @[Bitwise.scala 102:75]
  wire [15:0] _T_610 = _T_605 | _T_609; // @[Bitwise.scala 102:39]
  wire [7:0] _T_618 = {{4'd0}, _T_567[24:21]}; // @[Bitwise.scala 102:31]
  wire [7:0] _T_620 = {_T_567[20:17], 4'h0}; // @[Bitwise.scala 102:65]
  wire [7:0] _T_622 = _T_620 & 8'hf0; // @[Bitwise.scala 102:75]
  wire [7:0] _T_623 = _T_618 | _T_622; // @[Bitwise.scala 102:39]
  wire [7:0] _GEN_5 = {{2'd0}, _T_623[7:2]}; // @[Bitwise.scala 102:31]
  wire [7:0] _T_628 = _GEN_5 & 8'h33; // @[Bitwise.scala 102:31]
  wire [7:0] _T_630 = {_T_623[5:0], 2'h0}; // @[Bitwise.scala 102:65]
  wire [7:0] _T_632 = _T_630 & 8'hcc; // @[Bitwise.scala 102:75]
  wire [7:0] _T_633 = _T_628 | _T_632; // @[Bitwise.scala 102:39]
  wire [7:0] _GEN_6 = {{1'd0}, _T_633[7:1]}; // @[Bitwise.scala 102:31]
  wire [7:0] _T_638 = _GEN_6 & 8'h55; // @[Bitwise.scala 102:31]
  wire [7:0] _T_640 = {_T_633[6:0], 1'h0}; // @[Bitwise.scala 102:65]
  wire [7:0] _T_642 = _T_640 & 8'haa; // @[Bitwise.scala 102:75]
  wire [7:0] _T_643 = _T_638 | _T_642; // @[Bitwise.scala 102:39]
  wire [31:0] absSigSumExtraMask = {_T_610,_T_643,_T_567[25],_T_567[26],_T_567[27],_T_567[28],_T_567[29],_T_567[30],
    _T_567[31],1'h1}; // @[Cat.scala 30:58]
  wire [86:0] _T_667 = cFirstNormAbsSigSum[87:1] >> normTo2ShiftDist; // @[MulAddRecFN.scala 424:65]
  wire [31:0] _T_669 = ~cFirstNormAbsSigSum[31:0]; // @[MulAddRecFN.scala 427:19]
  wire [31:0] _T_670 = _T_669 & absSigSumExtraMask; // @[MulAddRecFN.scala 427:62]
  wire  _T_672 = _T_670 == 32'h0; // @[MulAddRecFN.scala 428:43]
  wire [31:0] _T_674 = cFirstNormAbsSigSum[31:0] & absSigSumExtraMask; // @[MulAddRecFN.scala 430:61]
  wire  _T_676 = _T_674 != 32'h0; // @[MulAddRecFN.scala 431:43]
  wire  _T_677 = doIncrSig ? _T_672 : _T_676; // @[MulAddRecFN.scala 426:16]
  wire [87:0] _T_678 = {_T_667,_T_677}; // @[Cat.scala 30:58]
  wire [56:0] sigX3 = _T_678[56:0]; // @[MulAddRecFN.scala 434:10]
  wire  sigX3Shift1 = sigX3[56:55] == 2'h0; // @[MulAddRecFN.scala 436:58]
  wire [13:0] _GEN_7 = {{6'd0}, estNormDist}; // @[MulAddRecFN.scala 437:40]
  wire [13:0] sExpX3 = io_fromPreMul_sExpSum - _GEN_7; // @[MulAddRecFN.scala 437:40]
  wire  isZeroY = sigX3[56:54] == 3'h0; // @[MulAddRecFN.scala 439:54]
  wire  _T_685 = io_fromPreMul_signProd ^ doNegSignSum; // @[MulAddRecFN.scala 444:36]
  wire  signY = isZeroY ? roundingMode_min : _T_685; // @[MulAddRecFN.scala 442:12]
  wire [12:0] sExpX3_13 = sExpX3[12:0]; // @[MulAddRecFN.scala 446:27]
  wire [55:0] _T_690 = sExpX3[13] ? 56'hffffffffffffff : 56'h0; // @[Bitwise.scala 71:12]
  wire [12:0] _T_691 = ~sExpX3_13; // @[primitives.scala 50:21]
  wire [64:0] _T_711 = 65'sh10000000000000000 >>> _T_691[5:0]; // @[primitives.scala 68:52]
  wire [31:0] _T_719 = {{16'd0}, _T_711[45:30]}; // @[Bitwise.scala 102:31]
  wire [31:0] _T_721 = {_T_711[29:14], 16'h0}; // @[Bitwise.scala 102:65]
  wire [31:0] _T_723 = _T_721 & 32'hffff0000; // @[Bitwise.scala 102:75]
  wire [31:0] _T_724 = _T_719 | _T_723; // @[Bitwise.scala 102:39]
  wire [31:0] _GEN_8 = {{8'd0}, _T_724[31:8]}; // @[Bitwise.scala 102:31]
  wire [31:0] _T_729 = _GEN_8 & 32'hff00ff; // @[Bitwise.scala 102:31]
  wire [31:0] _T_731 = {_T_724[23:0], 8'h0}; // @[Bitwise.scala 102:65]
  wire [31:0] _T_733 = _T_731 & 32'hff00ff00; // @[Bitwise.scala 102:75]
  wire [31:0] _T_734 = _T_729 | _T_733; // @[Bitwise.scala 102:39]
  wire [31:0] _GEN_9 = {{4'd0}, _T_734[31:4]}; // @[Bitwise.scala 102:31]
  wire [31:0] _T_739 = _GEN_9 & 32'hf0f0f0f; // @[Bitwise.scala 102:31]
  wire [31:0] _T_741 = {_T_734[27:0], 4'h0}; // @[Bitwise.scala 102:65]
  wire [31:0] _T_743 = _T_741 & 32'hf0f0f0f0; // @[Bitwise.scala 102:75]
  wire [31:0] _T_744 = _T_739 | _T_743; // @[Bitwise.scala 102:39]
  wire [31:0] _GEN_10 = {{2'd0}, _T_744[31:2]}; // @[Bitwise.scala 102:31]
  wire [31:0] _T_749 = _GEN_10 & 32'h33333333; // @[Bitwise.scala 102:31]
  wire [31:0] _T_751 = {_T_744[29:0], 2'h0}; // @[Bitwise.scala 102:65]
  wire [31:0] _T_753 = _T_751 & 32'hcccccccc; // @[Bitwise.scala 102:75]
  wire [31:0] _T_754 = _T_749 | _T_753; // @[Bitwise.scala 102:39]
  wire [31:0] _GEN_11 = {{1'd0}, _T_754[31:1]}; // @[Bitwise.scala 102:31]
  wire [31:0] _T_759 = _GEN_11 & 32'h55555555; // @[Bitwise.scala 102:31]
  wire [31:0] _T_761 = {_T_754[30:0], 1'h0}; // @[Bitwise.scala 102:65]
  wire [31:0] _T_763 = _T_761 & 32'haaaaaaaa; // @[Bitwise.scala 102:75]
  wire [31:0] _T_764 = _T_759 | _T_763; // @[Bitwise.scala 102:39]
  wire [15:0] _T_772 = {{8'd0}, _T_711[61:54]}; // @[Bitwise.scala 102:31]
  wire [15:0] _T_774 = {_T_711[53:46], 8'h0}; // @[Bitwise.scala 102:65]
  wire [15:0] _T_776 = _T_774 & 16'hff00; // @[Bitwise.scala 102:75]
  wire [15:0] _T_777 = _T_772 | _T_776; // @[Bitwise.scala 102:39]
  wire [15:0] _GEN_12 = {{4'd0}, _T_777[15:4]}; // @[Bitwise.scala 102:31]
  wire [15:0] _T_782 = _GEN_12 & 16'hf0f; // @[Bitwise.scala 102:31]
  wire [15:0] _T_784 = {_T_777[11:0], 4'h0}; // @[Bitwise.scala 102:65]
  wire [15:0] _T_786 = _T_784 & 16'hf0f0; // @[Bitwise.scala 102:75]
  wire [15:0] _T_787 = _T_782 | _T_786; // @[Bitwise.scala 102:39]
  wire [15:0] _GEN_13 = {{2'd0}, _T_787[15:2]}; // @[Bitwise.scala 102:31]
  wire [15:0] _T_792 = _GEN_13 & 16'h3333; // @[Bitwise.scala 102:31]
  wire [15:0] _T_794 = {_T_787[13:0], 2'h0}; // @[Bitwise.scala 102:65]
  wire [15:0] _T_796 = _T_794 & 16'hcccc; // @[Bitwise.scala 102:75]
  wire [15:0] _T_797 = _T_792 | _T_796; // @[Bitwise.scala 102:39]
  wire [15:0] _GEN_14 = {{1'd0}, _T_797[15:1]}; // @[Bitwise.scala 102:31]
  wire [15:0] _T_802 = _GEN_14 & 16'h5555; // @[Bitwise.scala 102:31]
  wire [15:0] _T_804 = {_T_797[14:0], 1'h0}; // @[Bitwise.scala 102:65]
  wire [15:0] _T_806 = _T_804 & 16'haaaa; // @[Bitwise.scala 102:75]
  wire [15:0] _T_807 = _T_802 | _T_806; // @[Bitwise.scala 102:39]
  wire [49:0] _T_813 = {_T_764,_T_807,_T_711[62],_T_711[63]}; // @[Cat.scala 30:58]
  wire [49:0] _T_814 = ~_T_813; // @[primitives.scala 65:36]
  wire [49:0] _T_815 = _T_691[6] ? 50'h0 : _T_814; // @[primitives.scala 65:21]
  wire [49:0] _T_816 = ~_T_815; // @[primitives.scala 65:17]
  wire [49:0] _T_817 = ~_T_816; // @[primitives.scala 65:36]
  wire [49:0] _T_818 = _T_691[7] ? 50'h0 : _T_817; // @[primitives.scala 65:21]
  wire [49:0] _T_819 = ~_T_818; // @[primitives.scala 65:17]
  wire [49:0] _T_820 = ~_T_819; // @[primitives.scala 65:36]
  wire [49:0] _T_821 = _T_691[8] ? 50'h0 : _T_820; // @[primitives.scala 65:21]
  wire [49:0] _T_822 = ~_T_821; // @[primitives.scala 65:17]
  wire [49:0] _T_823 = ~_T_822; // @[primitives.scala 65:36]
  wire [49:0] _T_824 = _T_691[9] ? 50'h0 : _T_823; // @[primitives.scala 65:21]
  wire [49:0] _T_825 = ~_T_824; // @[primitives.scala 65:17]
  wire [53:0] _T_827 = {_T_825,4'hf}; // @[Cat.scala 30:58]
  wire [3:0] _T_847 = {_T_711[0],_T_711[1],_T_711[2],_T_711[3]}; // @[Cat.scala 30:58]
  wire [3:0] _T_849 = _T_691[6] ? _T_847 : 4'h0; // @[primitives.scala 59:20]
  wire [3:0] _T_851 = _T_691[7] ? _T_849 : 4'h0; // @[primitives.scala 59:20]
  wire [3:0] _T_853 = _T_691[8] ? _T_851 : 4'h0; // @[primitives.scala 59:20]
  wire [3:0] _T_855 = _T_691[9] ? _T_853 : 4'h0; // @[primitives.scala 59:20]
  wire [53:0] _T_856 = _T_691[10] ? _T_827 : {{50'd0}, _T_855}; // @[primitives.scala 61:20]
  wire [53:0] _T_858 = _T_691[11] ? _T_856 : 54'h0; // @[primitives.scala 59:20]
  wire [53:0] _T_860 = _T_691[12] ? _T_858 : 54'h0; // @[primitives.scala 59:20]
  wire [53:0] _GEN_15 = {{53'd0}, sigX3[55]}; // @[MulAddRecFN.scala 449:75]
  wire [53:0] _T_862 = _T_860 | _GEN_15; // @[MulAddRecFN.scala 449:75]
  wire [55:0] _T_864 = {_T_862,2'h3}; // @[Cat.scala 30:58]
  wire [55:0] roundMask = _T_690 | _T_864; // @[MulAddRecFN.scala 448:50]
  wire [54:0] _T_866 = ~roundMask[55:1]; // @[MulAddRecFN.scala 454:24]
  wire [55:0] _GEN_16 = {{1'd0}, _T_866}; // @[MulAddRecFN.scala 454:40]
  wire [55:0] roundPosMask = _GEN_16 & roundMask; // @[MulAddRecFN.scala 454:40]
  wire [56:0] _GEN_17 = {{1'd0}, roundPosMask}; // @[MulAddRecFN.scala 455:30]
  wire [56:0] _T_867 = sigX3 & _GEN_17; // @[MulAddRecFN.scala 455:30]
  wire  roundPosBit = _T_867 != 57'h0; // @[MulAddRecFN.scala 455:46]
  wire [56:0] _GEN_18 = {{2'd0}, roundMask[55:1]}; // @[MulAddRecFN.scala 456:34]
  wire [56:0] _T_870 = sigX3 & _GEN_18; // @[MulAddRecFN.scala 456:34]
  wire  anyRoundExtra = _T_870 != 57'h0; // @[MulAddRecFN.scala 456:50]
  wire [56:0] _T_872 = ~sigX3; // @[MulAddRecFN.scala 457:27]
  wire [56:0] _T_874 = _T_872 & _GEN_18; // @[MulAddRecFN.scala 457:34]
  wire  allRoundExtra = _T_874 == 57'h0; // @[MulAddRecFN.scala 457:50]
  wire  anyRound = roundPosBit | anyRoundExtra; // @[MulAddRecFN.scala 458:32]
  wire  allRound = roundPosBit & allRoundExtra; // @[MulAddRecFN.scala 459:32]
  wire  roundDirectUp = signY ? roundingMode_min : roundingMode_max; // @[MulAddRecFN.scala 460:28]
  wire  _T_877 = ~doIncrSig; // @[MulAddRecFN.scala 462:10]
  wire  _T_879 = ~doIncrSig & roundingMode_nearest_even & roundPosBit; // @[MulAddRecFN.scala 462:51]
  wire  _T_884 = _T_877 & roundDirectUp & anyRound; // @[MulAddRecFN.scala 464:49]
  wire  _T_885 = _T_879 & anyRoundExtra | _T_884; // @[MulAddRecFN.scala 463:78]
  wire  _T_886 = doIncrSig & allRound; // @[MulAddRecFN.scala 465:49]
  wire  _T_887 = _T_885 | _T_886; // @[MulAddRecFN.scala 464:65]
  wire  _T_889 = doIncrSig & roundingMode_nearest_even & roundPosBit; // @[MulAddRecFN.scala 466:49]
  wire  _T_890 = _T_887 | _T_889; // @[MulAddRecFN.scala 465:65]
  wire  _T_891 = doIncrSig & roundDirectUp; // @[MulAddRecFN.scala 467:20]
  wire  roundUp = _T_890 | _T_891; // @[MulAddRecFN.scala 466:65]
  wire  _T_897 = roundingMode_nearest_even & ~roundPosBit & allRoundExtra; // @[MulAddRecFN.scala 470:56]
  wire  _T_901 = roundingMode_nearest_even & roundPosBit & ~anyRoundExtra; // @[MulAddRecFN.scala 471:56]
  wire  roundEven = doIncrSig ? _T_897 : _T_901; // @[MulAddRecFN.scala 469:12]
  wire  inexactY = doIncrSig ? ~allRound : anyRound; // @[MulAddRecFN.scala 473:27]
  wire [56:0] _GEN_20 = {{1'd0}, roundMask}; // @[MulAddRecFN.scala 475:18]
  wire [56:0] _T_904 = sigX3 | _GEN_20; // @[MulAddRecFN.scala 475:18]
  wire [54:0] roundUp_sigY3 = _T_904[56:2] + 55'h1; // @[MulAddRecFN.scala 475:35]
  wire [55:0] _T_914 = ~roundMask; // @[MulAddRecFN.scala 477:48]
  wire [56:0] _GEN_21 = {{1'd0}, _T_914}; // @[MulAddRecFN.scala 477:46]
  wire [56:0] _T_915 = sigX3 & _GEN_21; // @[MulAddRecFN.scala 477:46]
  wire [54:0] _T_918 = ~roundUp & ~roundEven ? _T_915[56:2] : 55'h0; // @[MulAddRecFN.scala 477:12]
  wire [54:0] _T_920 = roundUp ? roundUp_sigY3 : 55'h0; // @[MulAddRecFN.scala 478:12]
  wire [54:0] _T_921 = _T_918 | _T_920; // @[MulAddRecFN.scala 477:79]
  wire [54:0] _T_924 = roundUp_sigY3 & _T_866; // @[MulAddRecFN.scala 479:51]
  wire [54:0] _T_926 = roundEven ? _T_924 : 55'h0; // @[MulAddRecFN.scala 479:12]
  wire [54:0] sigY3 = _T_921 | _T_926; // @[MulAddRecFN.scala 478:79]
  wire [13:0] _T_930 = sExpX3 + 14'h1; // @[MulAddRecFN.scala 482:41]
  wire [13:0] _T_932 = sigY3[54] ? _T_930 : 14'h0; // @[MulAddRecFN.scala 482:12]
  wire [13:0] _T_935 = sigY3[53] ? sExpX3 : 14'h0; // @[MulAddRecFN.scala 483:12]
  wire [13:0] _T_936 = _T_932 | _T_935; // @[MulAddRecFN.scala 482:61]
  wire [13:0] _T_943 = sExpX3 - 14'h1; // @[MulAddRecFN.scala 485:20]
  wire [13:0] _T_945 = sigY3[54:53] == 2'h0 ? _T_943 : 14'h0; // @[MulAddRecFN.scala 484:12]
  wire [13:0] sExpY = _T_936 | _T_945; // @[MulAddRecFN.scala 483:61]
  wire [11:0] expY = sExpY[11:0]; // @[MulAddRecFN.scala 488:21]
  wire [51:0] fractY = sigX3Shift1 ? sigY3[51:0] : sigY3[52:1]; // @[MulAddRecFN.scala 490:12]
  wire  overflowY = sExpY[12:10] == 3'h3; // @[MulAddRecFN.scala 492:56]
  wire  _T_956 = sExpY[12] | expY < 12'h3ce; // @[MulAddRecFN.scala 496:34]
  wire  totalUnderflowY = ~isZeroY & _T_956; // @[MulAddRecFN.scala 495:19]
  wire [10:0] _T_960 = sigX3Shift1 ? 11'h402 : 11'h401; // @[MulAddRecFN.scala 501:26]
  wire [12:0] _GEN_22 = {{2'd0}, _T_960}; // @[MulAddRecFN.scala 500:29]
  wire  _T_961 = sExpX3_13 <= _GEN_22; // @[MulAddRecFN.scala 500:29]
  wire  _T_962 = sExpX3[13] | _T_961; // @[MulAddRecFN.scala 499:35]
  wire  underflowY = inexactY & _T_962; // @[MulAddRecFN.scala 498:22]
  wire  roundMagUp = roundingMode_min & signY | roundingMode_max & ~signY; // @[MulAddRecFN.scala 506:37]
  wire  overflowY_roundMagUp = roundingMode_nearest_even | roundMagUp; // @[MulAddRecFN.scala 507:58]
  wire  mulSpecial = isSpecialA | isSpecialB; // @[MulAddRecFN.scala 511:33]
  wire  addSpecial = mulSpecial | isSpecialC; // @[MulAddRecFN.scala 512:33]
  wire  notSpecial_addZeros = io_fromPreMul_isZeroProd & isZeroC; // @[MulAddRecFN.scala 513:56]
  wire  commonCase = ~addSpecial & ~notSpecial_addZeros; // @[MulAddRecFN.scala 514:35]
  wire  _T_979 = isInfA | isInfB; // @[MulAddRecFN.scala 518:46]
  wire  _T_982 = ~isNaNA & ~isNaNB & (isInfA | isInfB) & isInfC & doSubMags; // @[MulAddRecFN.scala 518:67]
  wire  notSigNaN_invalid = isInfA & isZeroB | isZeroA & isInfB | _T_982; // @[MulAddRecFN.scala 517:52]
  wire  invalid = isSigNaNA | isSigNaNB | isSigNaNC | notSigNaN_invalid; // @[MulAddRecFN.scala 519:55]
  wire  overflow = commonCase & overflowY; // @[MulAddRecFN.scala 520:32]
  wire  underflow = commonCase & underflowY; // @[MulAddRecFN.scala 521:32]
  wire  inexact = overflow | commonCase & inexactY; // @[MulAddRecFN.scala 522:28]
  wire  notSpecial_isZeroOut = notSpecial_addZeros | isZeroY | totalUnderflowY; // @[MulAddRecFN.scala 525:40]
  wire  pegMinFiniteMagOut = commonCase & totalUnderflowY & roundMagUp; // @[MulAddRecFN.scala 526:60]
  wire  pegMaxFiniteMagOut = overflow & ~overflowY_roundMagUp; // @[MulAddRecFN.scala 527:39]
  wire  notNaN_isInfOut = _T_979 | isInfC | overflow & overflowY_roundMagUp; // @[MulAddRecFN.scala 529:36]
  wire  isNaNOut = isNaNA | isNaNB | isNaNC | notSigNaN_invalid; // @[MulAddRecFN.scala 530:47]
  wire  _T_1001 = mulSpecial & ~isSpecialC & io_fromPreMul_signProd; // @[MulAddRecFN.scala 534:51]
  wire  _T_1002 = _T_452 & io_fromPreMul_opSignC | _T_1001; // @[MulAddRecFN.scala 533:78]
  wire  _T_1004 = ~mulSpecial; // @[MulAddRecFN.scala 535:10]
  wire  _T_1006 = ~mulSpecial & isSpecialC & io_fromPreMul_opSignC; // @[MulAddRecFN.scala 535:51]
  wire  _T_1007 = _T_1002 | _T_1006; // @[MulAddRecFN.scala 534:78]
  wire  _T_1012 = _T_1004 & notSpecial_addZeros & doSubMags & roundingMode_min; // @[MulAddRecFN.scala 536:59]
  wire  uncommonCaseSignOut = _T_1007 | _T_1012; // @[MulAddRecFN.scala 535:78]
  wire  signOut = ~isNaNOut & uncommonCaseSignOut | commonCase & signY; // @[MulAddRecFN.scala 538:55]
  wire [11:0] _T_1019 = notSpecial_isZeroOut ? 12'he00 : 12'h0; // @[MulAddRecFN.scala 541:18]
  wire [11:0] _T_1020 = ~_T_1019; // @[MulAddRecFN.scala 541:14]
  wire [11:0] _T_1021 = expY & _T_1020; // @[MulAddRecFN.scala 540:15]
  wire [11:0] _T_1025 = pegMinFiniteMagOut ? 12'hc31 : 12'h0; // @[MulAddRecFN.scala 545:18]
  wire [11:0] _T_1026 = ~_T_1025; // @[MulAddRecFN.scala 545:14]
  wire [11:0] _T_1027 = _T_1021 & _T_1026; // @[MulAddRecFN.scala 544:17]
  wire [11:0] _T_1030 = pegMaxFiniteMagOut ? 12'h400 : 12'h0; // @[MulAddRecFN.scala 549:18]
  wire [11:0] _T_1031 = ~_T_1030; // @[MulAddRecFN.scala 549:14]
  wire [11:0] _T_1032 = _T_1027 & _T_1031; // @[MulAddRecFN.scala 548:17]
  wire [11:0] _T_1035 = notNaN_isInfOut ? 12'h200 : 12'h0; // @[MulAddRecFN.scala 553:18]
  wire [11:0] _T_1036 = ~_T_1035; // @[MulAddRecFN.scala 553:14]
  wire [11:0] _T_1037 = _T_1032 & _T_1036; // @[MulAddRecFN.scala 552:17]
  wire [11:0] _T_1040 = pegMinFiniteMagOut ? 12'h3ce : 12'h0; // @[MulAddRecFN.scala 557:16]
  wire [11:0] _T_1041 = _T_1037 | _T_1040; // @[MulAddRecFN.scala 556:18]
  wire [11:0] _T_1044 = pegMaxFiniteMagOut ? 12'hbff : 12'h0; // @[MulAddRecFN.scala 558:16]
  wire [11:0] _T_1045 = _T_1041 | _T_1044; // @[MulAddRecFN.scala 557:74]
  wire [11:0] _T_1048 = notNaN_isInfOut ? 12'hc00 : 12'h0; // @[MulAddRecFN.scala 562:16]
  wire [11:0] _T_1049 = _T_1045 | _T_1048; // @[MulAddRecFN.scala 561:15]
  wire [11:0] _T_1052 = isNaNOut ? 12'he00 : 12'h0; // @[MulAddRecFN.scala 566:16]
  wire [11:0] expOut = _T_1049 | _T_1052; // @[MulAddRecFN.scala 565:15]
  wire [51:0] _T_1058 = isNaNOut ? 52'h8000000000000 : 52'h0; // @[MulAddRecFN.scala 569:16]
  wire [51:0] _T_1059 = totalUnderflowY & roundMagUp | isNaNOut ? _T_1058 : fractY; // @[MulAddRecFN.scala 568:12]
  wire [51:0] _T_1063 = pegMaxFiniteMagOut ? 52'hfffffffffffff : 52'h0; // @[Bitwise.scala 71:12]
  wire [51:0] fractOut = _T_1059 | _T_1063; // @[MulAddRecFN.scala 571:11]
  wire [12:0] _T_1064 = {signOut,expOut}; // @[Cat.scala 30:58]
  wire [1:0] _T_1067 = {underflow,inexact}; // @[Cat.scala 30:58]
  wire [2:0] _T_1069 = {invalid,1'h0,overflow}; // @[Cat.scala 30:58]
  assign io_out = {_T_1064,fractOut}; // @[Cat.scala 30:58]
  assign io_exceptionFlags = {_T_1069,_T_1067}; // @[Cat.scala 30:58]
endmodule
