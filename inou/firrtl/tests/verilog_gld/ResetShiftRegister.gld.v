module ResetShiftRegister(
  input        clock,
  input        reset,
  input  [3:0] io_in,
  input        io_shift,
  output [3:0] io_out
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_3;
`endif // RANDOMIZE_REG_INIT
  reg [3:0] r0; // @[ResetShiftRegister.scala 13:19]
  reg [3:0] r1; // @[ResetShiftRegister.scala 14:19]
  reg [3:0] r2; // @[ResetShiftRegister.scala 15:19]
  reg [3:0] r3; // @[ResetShiftRegister.scala 16:19]
  assign io_out = r3; // @[ResetShiftRegister.scala 23:10]
  always @(posedge clock) begin
    if (reset) begin // @[ResetShiftRegister.scala 13:19]
      r0 <= 4'h0; // @[ResetShiftRegister.scala 13:19]
    end else if (io_shift) begin // @[ResetShiftRegister.scala 17:19]
      r0 <= io_in; // @[ResetShiftRegister.scala 18:8]
    end
    if (reset) begin // @[ResetShiftRegister.scala 14:19]
      r1 <= 4'h0; // @[ResetShiftRegister.scala 14:19]
    end else if (io_shift) begin // @[ResetShiftRegister.scala 17:19]
      r1 <= r0; // @[ResetShiftRegister.scala 19:8]
    end
    if (reset) begin // @[ResetShiftRegister.scala 15:19]
      r2 <= 4'h0; // @[ResetShiftRegister.scala 15:19]
    end else if (io_shift) begin // @[ResetShiftRegister.scala 17:19]
      r2 <= r1; // @[ResetShiftRegister.scala 20:8]
    end
    if (reset) begin // @[ResetShiftRegister.scala 16:19]
      r3 <= 4'h0; // @[ResetShiftRegister.scala 16:19]
    end else if (io_shift) begin // @[ResetShiftRegister.scala 17:19]
      r3 <= r2; // @[ResetShiftRegister.scala 21:8]
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
  r0 = _RAND_0[3:0];
  _RAND_1 = {1{`RANDOM}};
  r1 = _RAND_1[3:0];
  _RAND_2 = {1{`RANDOM}};
  r2 = _RAND_2[3:0];
  _RAND_3 = {1{`RANDOM}};
  r3 = _RAND_3[3:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
