module VecShiftRegisterParam(
  input        clock,
  input        reset,
  input  [3:0] io_in,
  output [3:0] io_out
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
`endif // RANDOMIZE_REG_INIT
  reg [3:0] delays_0; // @[VecShiftRegisterParam.scala 19:23]
  reg [3:0] delays_1; // @[VecShiftRegisterParam.scala 19:23]
  reg [3:0] delays_2; // @[VecShiftRegisterParam.scala 19:23]
  reg [3:0] delays_3; // @[VecShiftRegisterParam.scala 19:23]
  reg [3:0] delays_4; // @[VecShiftRegisterParam.scala 19:23]
  reg [3:0] delays_5; // @[VecShiftRegisterParam.scala 19:23]
  reg [3:0] delays_6; // @[VecShiftRegisterParam.scala 19:23]
  reg [3:0] delays_7; // @[VecShiftRegisterParam.scala 19:23]
  assign io_out = delays_7; // @[VecShiftRegisterParam.scala 26:10]
  always @(posedge clock) begin
    if (reset) begin // @[VecShiftRegisterParam.scala 19:23]
      delays_0 <= 4'h0; // @[VecShiftRegisterParam.scala 19:23]
    end else begin
      delays_0 <= io_in; // @[VecShiftRegisterParam.scala 25:13]
    end
    if (reset) begin // @[VecShiftRegisterParam.scala 19:23]
      delays_1 <= 4'h0; // @[VecShiftRegisterParam.scala 19:23]
    end else begin
      delays_1 <= delays_0; // @[VecShiftRegisterParam.scala 22:15]
    end
    if (reset) begin // @[VecShiftRegisterParam.scala 19:23]
      delays_2 <= 4'h0; // @[VecShiftRegisterParam.scala 19:23]
    end else begin
      delays_2 <= delays_1; // @[VecShiftRegisterParam.scala 22:15]
    end
    if (reset) begin // @[VecShiftRegisterParam.scala 19:23]
      delays_3 <= 4'h0; // @[VecShiftRegisterParam.scala 19:23]
    end else begin
      delays_3 <= delays_2; // @[VecShiftRegisterParam.scala 22:15]
    end
    if (reset) begin // @[VecShiftRegisterParam.scala 19:23]
      delays_4 <= 4'h0; // @[VecShiftRegisterParam.scala 19:23]
    end else begin
      delays_4 <= delays_3; // @[VecShiftRegisterParam.scala 22:15]
    end
    if (reset) begin // @[VecShiftRegisterParam.scala 19:23]
      delays_5 <= 4'h0; // @[VecShiftRegisterParam.scala 19:23]
    end else begin
      delays_5 <= delays_4; // @[VecShiftRegisterParam.scala 22:15]
    end
    if (reset) begin // @[VecShiftRegisterParam.scala 19:23]
      delays_6 <= 4'h0; // @[VecShiftRegisterParam.scala 19:23]
    end else begin
      delays_6 <= delays_5; // @[VecShiftRegisterParam.scala 22:15]
    end
    if (reset) begin // @[VecShiftRegisterParam.scala 19:23]
      delays_7 <= 4'h0; // @[VecShiftRegisterParam.scala 19:23]
    end else begin
      delays_7 <= delays_6; // @[VecShiftRegisterParam.scala 22:15]
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
  delays_0 = _RAND_0[3:0];
  _RAND_1 = {1{`RANDOM}};
  delays_1 = _RAND_1[3:0];
  _RAND_2 = {1{`RANDOM}};
  delays_2 = _RAND_2[3:0];
  _RAND_3 = {1{`RANDOM}};
  delays_3 = _RAND_3[3:0];
  _RAND_4 = {1{`RANDOM}};
  delays_4 = _RAND_4[3:0];
  _RAND_5 = {1{`RANDOM}};
  delays_5 = _RAND_5[3:0];
  _RAND_6 = {1{`RANDOM}};
  delays_6 = _RAND_6[3:0];
  _RAND_7 = {1{`RANDOM}};
  delays_7 = _RAND_7[3:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
