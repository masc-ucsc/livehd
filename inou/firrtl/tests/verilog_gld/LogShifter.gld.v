module LogShifter(
  input         clock,
  input         reset,
  input  [15:0] io_in,
  input  [3:0]  io_shamt,
  output [15:0] io_out
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
`endif // RANDOMIZE_REG_INIT
  reg [15:0] s0; // @[LogShifter.scala 12:19]
  wire [23:0] _GEN_2 = {io_in, 8'h0}; // @[LogShifter.scala 14:17]
  wire [30:0] _s0_T = {{7'd0}, _GEN_2}; // @[LogShifter.scala 14:17]
  wire [30:0] _GEN_0 = io_shamt[3] ? _s0_T : {{15'd0}, io_in}; // @[LogShifter.scala 13:30 LogShifter.scala 14:8 LogShifter.scala 16:8]
  wire [16:0] _io_out_T = {s0, 1'h0}; // @[LogShifter.scala 36:18]
  wire [16:0] _GEN_1 = io_shamt[1] ? _io_out_T : {{1'd0}, s0}; // @[LogShifter.scala 35:30 LogShifter.scala 36:12 LogShifter.scala 38:12]
  assign io_out = _GEN_1[15:0];
  always @(posedge clock) begin
    if (reset) begin // @[LogShifter.scala 12:19]
      s0 <= 16'h0; // @[LogShifter.scala 12:19]
    end else begin
      s0 <= _GEN_0[15:0];
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
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
