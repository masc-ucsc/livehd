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
