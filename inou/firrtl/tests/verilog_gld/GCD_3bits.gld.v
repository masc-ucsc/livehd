module GCD_3bits(
  input        clock,
  input        reset,
  input  [2:0] io_value1,
  input  [2:0] io_value2,
  input        io_loadingValues,
  output [2:0] io_outputGCD,
  output       io_outputValid
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
`endif // RANDOMIZE_REG_INIT
  reg [2:0] x; // @[GCD_3bits.scala 21:15]
  reg [2:0] y; // @[GCD_3bits.scala 22:15]
  wire [2:0] _x_T_1 = x - y; // @[GCD_3bits.scala 24:24]
  wire [2:0] _y_T_1 = y - x; // @[GCD_3bits.scala 25:25]
  assign io_outputGCD = x; // @[GCD_3bits.scala 32:16]
  assign io_outputValid = y == 3'h0; // @[GCD_3bits.scala 33:23]
  always @(posedge clock) begin
    if (io_loadingValues) begin // @[GCD_3bits.scala 27:26]
      x <= io_value1; // @[GCD_3bits.scala 28:7]
    end else if (x > y) begin // @[GCD_3bits.scala 24:15]
      x <= _x_T_1; // @[GCD_3bits.scala 24:19]
    end
    if (io_loadingValues) begin // @[GCD_3bits.scala 27:26]
      y <= io_value2; // @[GCD_3bits.scala 29:7]
    end else if (!(x > y)) begin // @[GCD_3bits.scala 24:15]
      y <= _y_T_1; // @[GCD_3bits.scala 25:20]
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
  x = _RAND_0[2:0];
  _RAND_1 = {1{`RANDOM}};
  y = _RAND_1[2:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
