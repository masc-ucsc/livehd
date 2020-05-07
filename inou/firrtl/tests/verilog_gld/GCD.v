module GCD(
  input         clock,
  input         reset,
  input  [15:0] io_value1,
  input  [15:0] io_value2,
  input         io_loadingValues,
  output [15:0] io_outputGCD,
  output        io_outputValid
);
  reg [15:0] x; // @[GCD.scala 21:15]
  reg [31:0] _RAND_0;
  reg [15:0] y; // @[GCD.scala 22:15]
  reg [31:0] _RAND_1;
  wire  _T; // @[GCD.scala 24:10]
  wire [15:0] _T_2; // @[GCD.scala 24:24]
  wire [15:0] _T_4; // @[GCD.scala 25:25]
  assign _T = x > y; // @[GCD.scala 24:10]
  assign _T_2 = x - y; // @[GCD.scala 24:24]
  assign _T_4 = y - x; // @[GCD.scala 25:25]
  assign io_outputGCD = x; // @[GCD.scala 32:16]
  assign io_outputValid = y == 16'h0; // @[GCD.scala 33:18]
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
  x = _RAND_0[15:0];
  `endif // RANDOMIZE_REG_INIT
  `ifdef RANDOMIZE_REG_INIT
  _RAND_1 = {1{`RANDOM}};
  y = _RAND_1[15:0];
  `endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`endif // SYNTHESIS
  always @(posedge clock) begin
    if (io_loadingValues) begin
      x <= io_value1;
    end else if (_T) begin
      x <= _T_2;
    end
    if (io_loadingValues) begin
      y <= io_value2;
    end else if (!(_T)) begin
      y <= _T_4;
    end
  end
endmodule
