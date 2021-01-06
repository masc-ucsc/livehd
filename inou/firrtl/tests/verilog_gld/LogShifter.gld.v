module LogShifter(
  input         clock,
  input         reset,
  input  [15:0] io_in,
  input  [3:0]  io_shamt,
  output [15:0] io_out
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
`endif // RANDOMIZE_REG_INIT
  reg [15:0] s0; // @[LogShifter.scala 12:19]
  wire [23:0] _GEN_4 = {io_in, 8'h0}; // @[LogShifter.scala 14:17]
  wire [30:0] _T_2 = {{7'd0}, _GEN_4}; // @[LogShifter.scala 14:17]
  wire [30:0] _GEN_0 = io_shamt[3] ? _T_2 : {{15'd0}, io_in}; // @[LogShifter.scala 13:30 LogShifter.scala 14:8 LogShifter.scala 16:8]
  reg [15:0] s1; // @[LogShifter.scala 18:19]
  wire [19:0] _GEN_5 = {s0, 4'h0}; // @[LogShifter.scala 20:14]
  wire [22:0] _T_5 = {{3'd0}, _GEN_5}; // @[LogShifter.scala 20:14]
  wire [22:0] _GEN_1 = io_shamt[2] ? _T_5 : {{7'd0}, s0}; // @[LogShifter.scala 19:30 LogShifter.scala 20:8 LogShifter.scala 22:8]
  reg [15:0] s2; // @[LogShifter.scala 24:19]
  wire [17:0] _GEN_6 = {s1, 2'h0}; // @[LogShifter.scala 26:14]
  wire [18:0] _T_8 = {{1'd0}, _GEN_6}; // @[LogShifter.scala 26:14]
  wire [18:0] _GEN_2 = io_shamt[1] ? _T_8 : {{3'd0}, s1}; // @[LogShifter.scala 25:30 LogShifter.scala 26:8 LogShifter.scala 28:8]
  wire [16:0] _T_11 = {s2, 1'h0}; // @[LogShifter.scala 31:18]
  wire [16:0] _GEN_3 = io_shamt[1] ? _T_11 : {{1'd0}, s2}; // @[LogShifter.scala 30:30 LogShifter.scala 31:12 LogShifter.scala 33:12]
  assign io_out = _GEN_3[15:0];
  always @(posedge clock) begin
    if (reset) begin // @[LogShifter.scala 12:19]
      s0 <= 16'h0; // @[LogShifter.scala 12:19]
    end else begin
      s0 <= _GEN_0[15:0];
    end
    if (reset) begin // @[LogShifter.scala 18:19]
      s1 <= 16'h0; // @[LogShifter.scala 18:19]
    end else begin
      s1 <= _GEN_1[15:0];
    end
    if (reset) begin // @[LogShifter.scala 24:19]
      s2 <= 16'h0; // @[LogShifter.scala 24:19]
    end else begin
      s2 <= _GEN_2[15:0];
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
  s0 = _RAND_0[15:0];
  _RAND_1 = {1{`RANDOM}};
  s1 = _RAND_1[15:0];
  _RAND_2 = {1{`RANDOM}};
  s2 = _RAND_2[15:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
