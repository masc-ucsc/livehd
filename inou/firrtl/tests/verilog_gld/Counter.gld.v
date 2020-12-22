module Counter(
  input        clock,
  input        reset,
  input        io_inc,
  input  [3:0] io_amt,
  output [7:0] io_tot
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
`endif // RANDOMIZE_REG_INIT
  reg [7:0] _T; // @[Counter.scala 18:20]
  wire [7:0] _GEN_1 = {{4'd0}, io_amt}; // @[Counter.scala 19:35]
  wire [7:0] _T_2 = _T + _GEN_1; // @[Counter.scala 19:35]
  assign io_tot = _T; // @[Counter.scala 32:10]
  always @(posedge clock) begin
    if (reset) begin // @[Counter.scala 18:20]
      _T <= 8'h0; // @[Counter.scala 18:20]
    end else if (io_inc) begin // @[Counter.scala 19:15]
      _T <= _T_2; // @[Counter.scala 19:19]
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
  _T = _RAND_0[7:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
