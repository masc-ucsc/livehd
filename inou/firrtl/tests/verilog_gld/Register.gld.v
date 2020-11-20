module Register(
  input         clock,
  input         reset,
  input  [15:0] io_inVal,
  input         io_loadingValues,
  output [15:0] io_outVal
);
  reg [15:0] x; // @[Register.scala 19:15]
  reg [31:0] _RAND_0;
  wire  _T; // @[Register.scala 24:13]
  wire [15:0] _T_2; // @[Register.scala 25:14]
  assign _T = x > 16'h0; // @[Register.scala 24:13]
  assign _T_2 = x - 16'h1; // @[Register.scala 25:14]
  assign io_outVal = x; // @[Register.scala 29:13]
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
  `endif // RANDOMIZE
end // initial
`endif // SYNTHESIS
  always @(posedge clock) begin
    if (io_loadingValues) begin
      x <= io_inVal;
    end else if (_T) begin
      x <= _T_2;
    end
  end
endmodule
