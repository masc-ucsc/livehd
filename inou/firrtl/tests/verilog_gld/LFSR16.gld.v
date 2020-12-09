module LFSR16(
  input         clock,
  input         reset,
  input         io_inc,
  output [15:0] io_out
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
`endif // RANDOMIZE_REG_INIT
  reg [15:0] res; // @[LFSR16.scala 19:20]
  wire  _T_6 = res[0] ^ res[2] ^ res[3] ^ res[5]; // @[LFSR16.scala 21:43]
  wire [15:0] _T_8 = {_T_6,res[15:1]}; // @[Cat.scala 29:58]
  assign io_out = res; // @[LFSR16.scala 24:10]
  always @(posedge clock) begin
    if (reset) begin // @[LFSR16.scala 19:20]
      res <= 16'h1; // @[LFSR16.scala 19:20]
    end else if (io_inc) begin // @[LFSR16.scala 20:17]
      res <= _T_8; // @[LFSR16.scala 22:9]
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
  res = _RAND_0[15:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
