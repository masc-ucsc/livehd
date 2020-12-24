module VecShiftRegisterSimple(
  input        clock,
  input        reset,
  input  [7:0] io_in,
  output [7:0] io_out
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_3;
`endif // RANDOMIZE_REG_INIT
  reg [7:0] delays_0; // @[VecShiftRegisterSimple.scala 18:23]
  reg [7:0] delays_1; // @[VecShiftRegisterSimple.scala 18:23]
  reg [7:0] delays_2; // @[VecShiftRegisterSimple.scala 18:23]
  reg [7:0] delays_3; // @[VecShiftRegisterSimple.scala 18:23]
  assign io_out = delays_3; // @[VecShiftRegisterSimple.scala 24:13]
  always @(posedge clock) begin
    if (reset) begin // @[VecShiftRegisterSimple.scala 18:23]
      delays_0 <= 8'h0; // @[VecShiftRegisterSimple.scala 18:23]
    end else begin
      delays_0 <= io_in; // @[VecShiftRegisterSimple.scala 20:13]
    end
    if (reset) begin // @[VecShiftRegisterSimple.scala 18:23]
      delays_1 <= 8'h0; // @[VecShiftRegisterSimple.scala 18:23]
    end else begin
      delays_1 <= delays_0; // @[VecShiftRegisterSimple.scala 21:13]
    end
    if (reset) begin // @[VecShiftRegisterSimple.scala 18:23]
      delays_2 <= 8'h0; // @[VecShiftRegisterSimple.scala 18:23]
    end else begin
      delays_2 <= delays_1; // @[VecShiftRegisterSimple.scala 22:13]
    end
    if (reset) begin // @[VecShiftRegisterSimple.scala 18:23]
      delays_3 <= 8'h0; // @[VecShiftRegisterSimple.scala 18:23]
    end else begin
      delays_3 <= delays_2; // @[VecShiftRegisterSimple.scala 23:13]
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
  delays_0 = _RAND_0[7:0];
  _RAND_1 = {1{`RANDOM}};
  delays_1 = _RAND_1[7:0];
  _RAND_2 = {1{`RANDOM}};
  delays_2 = _RAND_2[7:0];
  _RAND_3 = {1{`RANDOM}};
  delays_3 = _RAND_3[7:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
