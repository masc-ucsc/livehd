module MemoryController(
  input        clock,
  input        reset,
  output       io_ddr3_casN,
  output       io_ddr3_rasN,
  output [2:0] io_ddr3_ba
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
`endif // RANDOMIZE_REG_INIT
  reg  nextDDR3Cmd_casN; // @[MemoryController.scala 27:28]
  reg [2:0] nextDDR3Cmd_ba; // @[MemoryController.scala 27:28]
  assign io_ddr3_casN = nextDDR3Cmd_casN; // @[MemoryController.scala 36:11]
  assign io_ddr3_rasN = 1'h1; // @[MemoryController.scala 36:11]
  assign io_ddr3_ba = nextDDR3Cmd_ba; // @[MemoryController.scala 36:11]
  always @(posedge clock) begin
    nextDDR3Cmd_casN <= reset;
    if (reset) begin
      nextDDR3Cmd_ba <= 3'h0;
    end else begin
      nextDDR3Cmd_ba <= 3'h3;
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
  nextDDR3Cmd_casN = _RAND_0[0:0];
  _RAND_1 = {1{`RANDOM}};
  nextDDR3Cmd_ba = _RAND_1[2:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
