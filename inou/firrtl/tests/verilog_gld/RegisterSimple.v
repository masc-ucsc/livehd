module RegisterSimple(
  input         clock,
  input         reset,
  input  [15:0] io_inVal,
  output [15:0] io_outVal
);
  reg [15:0] x; // @[RegisterSimple.scala 18:15]
  reg [31:0] _RAND_0;
  wire  _T; // @[RegisterSimple.scala 20:11]
  wire [15:0] _T_2; // @[RegisterSimple.scala 23:12]
  assign _T = x == 16'h0; // @[RegisterSimple.scala 20:11]
  assign _T_2 = x - 16'h1; // @[RegisterSimple.scala 23:12]
  assign io_outVal = x; // @[RegisterSimple.scala 26:13]
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
    if (_T) begin
      x <= io_inVal;
    end else begin
      x <= _T_2;
    end
  end
endmodule
