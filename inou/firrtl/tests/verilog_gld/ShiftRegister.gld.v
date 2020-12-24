module ShiftRegister(
  input   clock,
  input   reset,
  input   io_in,
  output  io_out
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_3;
`endif // RANDOMIZE_REG_INIT
  reg  r0; // @[ShiftRegister.scala 11:19]
  reg  r1; // @[ShiftRegister.scala 12:19]
  reg  r2; // @[ShiftRegister.scala 13:19]
  reg  r3; // @[ShiftRegister.scala 14:19]
  assign io_out = r3; // @[ShiftRegister.scala 15:10]
  always @(posedge clock) begin
    r0 <= io_in; // @[ShiftRegister.scala 11:19]
    r1 <= r0; // @[ShiftRegister.scala 12:19]
    r2 <= r1; // @[ShiftRegister.scala 13:19]
    r3 <= r2; // @[ShiftRegister.scala 14:19]
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
  r0 = _RAND_0[0:0];
  _RAND_1 = {1{`RANDOM}};
  r1 = _RAND_1[0:0];
  _RAND_2 = {1{`RANDOM}};
  r2 = _RAND_2[0:0];
  _RAND_3 = {1{`RANDOM}};
  r3 = _RAND_3[0:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
