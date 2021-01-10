module Cell(
  input   clock,
  input   reset,
  input   io_neighbors_0,
  input   io_neighbors_1,
  input   io_neighbors_2,
  input   io_neighbors_3,
  input   io_neighbors_4,
  input   io_neighbors_5,
  input   io_neighbors_6,
  input   io_neighbors_7,
  output  io_out,
  input   io_running,
  input   io_writeEnable,
  input   io_writeValue
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
`endif // RANDOMIZE_REG_INIT
  reg  isAlive; // @[Life.scala 16:24]
  wire [3:0] _T_1 = {{3'd0}, io_neighbors_7}; // @[Life.scala 26:81]
  wire [2:0] _GEN_6 = {{2'd0}, io_neighbors_6}; // @[Life.scala 26:81]
  wire [2:0] _T_4 = _GEN_6 + _T_1[2:0]; // @[Life.scala 26:81]
  wire [2:0] _GEN_7 = {{2'd0}, io_neighbors_5}; // @[Life.scala 26:81]
  wire [2:0] _T_6 = _GEN_7 + _T_4; // @[Life.scala 26:81]
  wire [2:0] _GEN_8 = {{2'd0}, io_neighbors_4}; // @[Life.scala 26:81]
  wire [2:0] _T_8 = _GEN_8 + _T_6; // @[Life.scala 26:81]
  wire [2:0] _GEN_9 = {{2'd0}, io_neighbors_3}; // @[Life.scala 26:81]
  wire [2:0] _T_10 = _GEN_9 + _T_8; // @[Life.scala 26:81]
  wire [2:0] _GEN_10 = {{2'd0}, io_neighbors_2}; // @[Life.scala 26:81]
  wire [2:0] _T_12 = _GEN_10 + _T_10; // @[Life.scala 26:81]
  wire [2:0] _GEN_11 = {{2'd0}, io_neighbors_1}; // @[Life.scala 26:81]
  wire [2:0] _T_14 = _GEN_11 + _T_12; // @[Life.scala 26:81]
  wire [2:0] _GEN_12 = {{2'd0}, io_neighbors_0}; // @[Life.scala 26:81]
  wire [2:0] _T_16 = _GEN_12 + _T_14; // @[Life.scala 26:81]
  wire  _T_18 = _T_16 < 3'h4; // @[Life.scala 31:24]
  wire  _T_21 = ~isAlive & _T_16 == 3'h3; // @[Life.scala 37:21]
  assign io_out = isAlive; // @[Life.scala 46:10]
  always @(posedge clock) begin
    if (reset) begin // @[Life.scala 16:24]
      isAlive <= 1'h0; // @[Life.scala 16:24]
    end else if (~io_running) begin // @[Life.scala 18:21]
      if (io_writeEnable) begin // @[Life.scala 19:26]
        isAlive <= io_writeValue; // @[Life.scala 20:15]
      end
    end else if (isAlive) begin // @[Life.scala 28:19]
      if (_T_16 < 3'h2) begin // @[Life.scala 29:25]
        isAlive <= 1'h0; // @[Life.scala 30:17]
      end else begin
        isAlive <= _T_18;
      end
    end else begin
      isAlive <= _T_21;
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
  isAlive = _RAND_0[0:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module Life(
  input        clock,
  input        reset,
  output       io_state_0_0,
  output       io_state_1_0,
  input        io_running,
  input        io_writeValue,
  input  [1:0] io_writeRowAddress,
  input        io_writeColAddress
);
  wire  Cell_clock; // @[Life.scala 59:52]
  wire  Cell_reset; // @[Life.scala 59:52]
  wire  Cell_io_neighbors_0; // @[Life.scala 59:52]
  wire  Cell_io_neighbors_1; // @[Life.scala 59:52]
  wire  Cell_io_neighbors_2; // @[Life.scala 59:52]
  wire  Cell_io_neighbors_3; // @[Life.scala 59:52]
  wire  Cell_io_neighbors_4; // @[Life.scala 59:52]
  wire  Cell_io_neighbors_5; // @[Life.scala 59:52]
  wire  Cell_io_neighbors_6; // @[Life.scala 59:52]
  wire  Cell_io_neighbors_7; // @[Life.scala 59:52]
  wire  Cell_io_out; // @[Life.scala 59:52]
  wire  Cell_io_running; // @[Life.scala 59:52]
  wire  Cell_io_writeEnable; // @[Life.scala 59:52]
  wire  Cell_io_writeValue; // @[Life.scala 59:52]
  wire  Cell_1_clock; // @[Life.scala 59:52]
  wire  Cell_1_reset; // @[Life.scala 59:52]
  wire  Cell_1_io_neighbors_0; // @[Life.scala 59:52]
  wire  Cell_1_io_neighbors_1; // @[Life.scala 59:52]
  wire  Cell_1_io_neighbors_2; // @[Life.scala 59:52]
  wire  Cell_1_io_neighbors_3; // @[Life.scala 59:52]
  wire  Cell_1_io_neighbors_4; // @[Life.scala 59:52]
  wire  Cell_1_io_neighbors_5; // @[Life.scala 59:52]
  wire  Cell_1_io_neighbors_6; // @[Life.scala 59:52]
  wire  Cell_1_io_neighbors_7; // @[Life.scala 59:52]
  wire  Cell_1_io_out; // @[Life.scala 59:52]
  wire  Cell_1_io_running; // @[Life.scala 59:52]
  wire  Cell_1_io_writeEnable; // @[Life.scala 59:52]
  wire  Cell_1_io_writeValue; // @[Life.scala 59:52]
  Cell Cell ( // @[Life.scala 59:52]
    .clock(Cell_clock),
    .reset(Cell_reset),
    .io_neighbors_0(Cell_io_neighbors_0),
    .io_neighbors_1(Cell_io_neighbors_1),
    .io_neighbors_2(Cell_io_neighbors_2),
    .io_neighbors_3(Cell_io_neighbors_3),
    .io_neighbors_4(Cell_io_neighbors_4),
    .io_neighbors_5(Cell_io_neighbors_5),
    .io_neighbors_6(Cell_io_neighbors_6),
    .io_neighbors_7(Cell_io_neighbors_7),
    .io_out(Cell_io_out),
    .io_running(Cell_io_running),
    .io_writeEnable(Cell_io_writeEnable),
    .io_writeValue(Cell_io_writeValue)
  );
  Cell Cell_1 ( // @[Life.scala 59:52]
    .clock(Cell_1_clock),
    .reset(Cell_1_reset),
    .io_neighbors_0(Cell_1_io_neighbors_0),
    .io_neighbors_1(Cell_1_io_neighbors_1),
    .io_neighbors_2(Cell_1_io_neighbors_2),
    .io_neighbors_3(Cell_1_io_neighbors_3),
    .io_neighbors_4(Cell_1_io_neighbors_4),
    .io_neighbors_5(Cell_1_io_neighbors_5),
    .io_neighbors_6(Cell_1_io_neighbors_6),
    .io_neighbors_7(Cell_1_io_neighbors_7),
    .io_out(Cell_1_io_out),
    .io_running(Cell_1_io_running),
    .io_writeEnable(Cell_1_io_writeEnable),
    .io_writeValue(Cell_1_io_writeValue)
  );
  assign io_state_0_0 = Cell_io_out; // @[Life.scala 65:24]
  assign io_state_1_0 = Cell_1_io_out; // @[Life.scala 65:24]
  assign Cell_clock = clock;
  assign Cell_reset = reset;
  assign Cell_io_neighbors_0 = Cell_1_io_out; // @[Life.scala 89:46]
  assign Cell_io_neighbors_1 = Cell_1_io_out; // @[Life.scala 89:46]
  assign Cell_io_neighbors_2 = Cell_1_io_out; // @[Life.scala 89:46]
  assign Cell_io_neighbors_3 = Cell_io_out; // @[Life.scala 89:46]
  assign Cell_io_neighbors_4 = Cell_io_out; // @[Life.scala 89:46]
  assign Cell_io_neighbors_5 = Cell_1_io_out; // @[Life.scala 89:46]
  assign Cell_io_neighbors_6 = Cell_1_io_out; // @[Life.scala 89:46]
  assign Cell_io_neighbors_7 = Cell_1_io_out; // @[Life.scala 89:46]
  assign Cell_io_running = io_running; // @[Life.scala 66:32]
  assign Cell_io_writeEnable = io_writeRowAddress == 2'h0 & ~io_writeColAddress; // @[Life.scala 68:68]
  assign Cell_io_writeValue = io_writeValue; // @[Life.scala 67:35]
  assign Cell_1_clock = clock;
  assign Cell_1_reset = reset;
  assign Cell_1_io_neighbors_0 = Cell_io_out; // @[Life.scala 89:46]
  assign Cell_1_io_neighbors_1 = Cell_io_out; // @[Life.scala 89:46]
  assign Cell_1_io_neighbors_2 = Cell_io_out; // @[Life.scala 89:46]
  assign Cell_1_io_neighbors_3 = Cell_1_io_out; // @[Life.scala 89:46]
  assign Cell_1_io_neighbors_4 = Cell_1_io_out; // @[Life.scala 89:46]
  assign Cell_1_io_neighbors_5 = Cell_io_out; // @[Life.scala 89:46]
  assign Cell_1_io_neighbors_6 = Cell_io_out; // @[Life.scala 89:46]
  assign Cell_1_io_neighbors_7 = Cell_io_out; // @[Life.scala 89:46]
  assign Cell_1_io_running = io_running; // @[Life.scala 66:32]
  assign Cell_1_io_writeEnable = io_writeRowAddress == 2'h1 & ~io_writeColAddress; // @[Life.scala 68:68]
  assign Cell_1_io_writeValue = io_writeValue; // @[Life.scala 67:35]
endmodule
