module VecSearch(
  input        clock,
  input        reset,
  output [3:0] io_out
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
`endif // RANDOMIZE_REG_INIT
  reg [2:0] index; // @[VecSearch.scala 14:22]
  wire [2:0] _T_1 = index + 3'h1; // @[VecSearch.scala 16:18]
  wire [3:0] _GEN_1 = 3'h1 == index ? 4'h4 : 4'h0; // @[VecSearch.scala 17:10 VecSearch.scala 17:10]
  wire [3:0] _GEN_2 = 3'h2 == index ? 4'hf : _GEN_1; // @[VecSearch.scala 17:10 VecSearch.scala 17:10]
  wire [3:0] _GEN_3 = 3'h3 == index ? 4'he : _GEN_2; // @[VecSearch.scala 17:10 VecSearch.scala 17:10]
  wire [3:0] _GEN_4 = 3'h4 == index ? 4'h2 : _GEN_3; // @[VecSearch.scala 17:10 VecSearch.scala 17:10]
  wire [3:0] _GEN_5 = 3'h5 == index ? 4'h5 : _GEN_4; // @[VecSearch.scala 17:10 VecSearch.scala 17:10]
  assign io_out = 3'h6 == index ? 4'hd : _GEN_5; // @[VecSearch.scala 17:10 VecSearch.scala 17:10]
  always @(posedge clock) begin
    if (reset) begin // @[VecSearch.scala 14:22]
      index <= 3'h0; // @[VecSearch.scala 14:22]
    end else begin
      index <= _T_1; // @[VecSearch.scala 16:9]
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
  index = _RAND_0[2:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
