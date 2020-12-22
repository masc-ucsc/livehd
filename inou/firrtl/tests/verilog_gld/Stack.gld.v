module Stack(
  input         clock,
  input         reset,
  input         io_push,
  input         io_pop,
  input         io_en,
  input  [31:0] io_dataIn,
  output [31:0] io_dataOut
);
`ifdef RANDOMIZE_MEM_INIT
  reg [31:0] _RAND_0;
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
`endif // RANDOMIZE_REG_INIT
  reg [31:0] stack_mem [0:7]; // @[Stack.scala 16:22]
  wire [31:0] stack_mem__T_14_data; // @[Stack.scala 16:22]
  wire [2:0] stack_mem__T_14_addr; // @[Stack.scala 16:22]
  wire [31:0] stack_mem__T_3_data; // @[Stack.scala 16:22]
  wire [2:0] stack_mem__T_3_addr; // @[Stack.scala 16:22]
  wire  stack_mem__T_3_mask; // @[Stack.scala 16:22]
  wire  stack_mem__T_3_en; // @[Stack.scala 16:22]
  reg [3:0] sp; // @[Stack.scala 17:26]
  reg [31:0] out; // @[Stack.scala 18:26]
  wire  _T_1 = io_push & sp < 4'h8; // @[Stack.scala 21:18]
  wire [3:0] _T_5 = sp + 4'h1; // @[Stack.scala 23:16]
  wire  _T_6 = sp > 4'h0; // @[Stack.scala 24:31]
  wire [3:0] _T_9 = sp - 4'h1; // @[Stack.scala 25:16]
  assign stack_mem__T_14_addr = _T_9[2:0];
  assign stack_mem__T_14_data = stack_mem[stack_mem__T_14_addr]; // @[Stack.scala 16:22]
  assign stack_mem__T_3_data = io_dataIn;
  assign stack_mem__T_3_addr = sp[2:0];
  assign stack_mem__T_3_mask = 1'h1;
  assign stack_mem__T_3_en = io_en & _T_1;
  assign io_dataOut = out; // @[Stack.scala 32:14]
  always @(posedge clock) begin
    if(stack_mem__T_3_en & stack_mem__T_3_mask) begin
      stack_mem[stack_mem__T_3_addr] <= stack_mem__T_3_data; // @[Stack.scala 16:22]
    end
    if (reset) begin // @[Stack.scala 17:26]
      sp <= 4'h0; // @[Stack.scala 17:26]
    end else if (io_en) begin // @[Stack.scala 20:16]
      if (io_push & sp < 4'h8) begin // @[Stack.scala 21:42]
        sp <= _T_5; // @[Stack.scala 23:10]
      end else if (io_pop & sp > 4'h0) begin // @[Stack.scala 24:39]
        sp <= _T_9; // @[Stack.scala 25:10]
      end
    end
    if (reset) begin // @[Stack.scala 18:26]
      out <= 32'h0; // @[Stack.scala 18:26]
    end else if (io_en) begin // @[Stack.scala 20:16]
      if (_T_6) begin // @[Stack.scala 27:21]
        out <= stack_mem__T_14_data; // @[Stack.scala 28:11]
      end
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
`ifdef RANDOMIZE_MEM_INIT
  _RAND_0 = {1{`RANDOM}};
  for (initvar = 0; initvar < 8; initvar = initvar+1)
    stack_mem[initvar] = _RAND_0[31:0];
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  _RAND_1 = {1{`RANDOM}};
  sp = _RAND_1[3:0];
  _RAND_2 = {1{`RANDOM}};
  out = _RAND_2[31:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
