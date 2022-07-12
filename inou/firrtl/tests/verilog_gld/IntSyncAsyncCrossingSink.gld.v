module NonSyncResetSynchronizerPrimitiveShiftReg_d3(
  input   clock,
  input   io_d,
  output  io_q
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
`endif // RANDOMIZE_REG_INIT
  reg  sync_0; // @[SynchronizerReg.scala 59:68]
  reg  sync_1; // @[SynchronizerReg.scala 59:68]
  reg  sync_2; // @[SynchronizerReg.scala 59:68]
  assign io_q = sync_0; // @[SynchronizerReg.scala 67:10]
  always @(posedge clock) begin
    sync_0 <= sync_1; // @[SynchronizerReg.scala 65:12]
    sync_1 <= sync_2; // @[SynchronizerReg.scala 65:12]
    sync_2 <= io_d; // @[SynchronizerReg.scala 62:24]
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
  sync_0 = _RAND_0[0:0];
  _RAND_1 = {1{`RANDOM}};
  sync_1 = _RAND_1[0:0];
  _RAND_2 = {1{`RANDOM}};
  sync_2 = _RAND_2[0:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module SynchronizerShiftReg_w1_d3(
  input   clock,
  input   io_d,
  output  io_q
);
  wire  NonSyncResetSynchronizerPrimitiveShiftReg_d3_clock; // @[ShiftReg.scala 45:23]
  wire  NonSyncResetSynchronizerPrimitiveShiftReg_d3_io_d; // @[ShiftReg.scala 45:23]
  wire  NonSyncResetSynchronizerPrimitiveShiftReg_d3_io_q; // @[ShiftReg.scala 45:23]
  NonSyncResetSynchronizerPrimitiveShiftReg_d3 NonSyncResetSynchronizerPrimitiveShiftReg_d3 ( // @[ShiftReg.scala 45:23]
    .clock(NonSyncResetSynchronizerPrimitiveShiftReg_d3_clock),
    .io_d(NonSyncResetSynchronizerPrimitiveShiftReg_d3_io_d),
    .io_q(NonSyncResetSynchronizerPrimitiveShiftReg_d3_io_q)
  );
  assign io_q = NonSyncResetSynchronizerPrimitiveShiftReg_d3_io_q; // @[ShiftReg.scala 48:{24,24}]
  assign NonSyncResetSynchronizerPrimitiveShiftReg_d3_clock = clock;
  assign NonSyncResetSynchronizerPrimitiveShiftReg_d3_io_d = io_d; // @[SynchronizerReg.scala 163:39]
endmodule
module IntSyncAsyncCrossingSink(
  input   clock,
  input   reset,
  input   auto_in_sync_0,
  output  auto_out_0
);
  wire  SynchronizerShiftReg_w1_d3_clock; // @[ShiftReg.scala 45:23]
  wire  SynchronizerShiftReg_w1_d3_io_d; // @[ShiftReg.scala 45:23]
  wire  SynchronizerShiftReg_w1_d3_io_q; // @[ShiftReg.scala 45:23]
  SynchronizerShiftReg_w1_d3 SynchronizerShiftReg_w1_d3 ( // @[ShiftReg.scala 45:23]
    .clock(SynchronizerShiftReg_w1_d3_clock),
    .io_d(SynchronizerShiftReg_w1_d3_io_d),
    .io_q(SynchronizerShiftReg_w1_d3_io_q)
  );
  assign auto_out_0 = SynchronizerShiftReg_w1_d3_io_q; // @[ShiftReg.scala 48:24]
  assign SynchronizerShiftReg_w1_d3_clock = clock;
  assign SynchronizerShiftReg_w1_d3_io_d = auto_in_sync_0; // @[LazyModule.scala 181:31 Nodes.scala 389:84]
endmodule
