module Memo(
  input        clock,
  input        reset,
  input        io_wen,
  input  [7:0] io_wrAddr,
  input  [7:0] io_wrData,
  input        io_ren,
  input  [7:0] io_rdAddr,
  output [7:0] io_rdData
);
`ifdef RANDOMIZE_MEM_INIT
  reg [31:0] _RAND_0;
`endif // RANDOMIZE_MEM_INIT
  reg [7:0] mem [0:255]; // @[Memo.scala 23:16]
  wire [7:0] mem__T_1_data; // @[Memo.scala 23:16]
  wire [7:0] mem__T_1_addr; // @[Memo.scala 23:16]
  wire [7:0] mem__T_data; // @[Memo.scala 23:16]
  wire [7:0] mem__T_addr; // @[Memo.scala 23:16]
  wire  mem__T_mask; // @[Memo.scala 23:16]
  wire  mem__T_en; // @[Memo.scala 23:16]
  assign mem__T_1_addr = io_rdAddr;
  assign mem__T_1_data = mem[mem__T_1_addr]; // @[Memo.scala 23:16]
  assign mem__T_data = io_wrData;
  assign mem__T_addr = io_wrAddr;
  assign mem__T_mask = 1'h1;
  assign mem__T_en = io_wen;
  assign io_rdData = io_ren ? mem__T_1_data : 8'h0; // @[Memo.scala 30:17 Memo.scala 30:29 Memo.scala 29:13]
  always @(posedge clock) begin
    if(mem__T_en & mem__T_mask) begin
      mem[mem__T_addr] <= mem__T_data; // @[Memo.scala 23:16]
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
  for (initvar = 0; initvar < 256; initvar = initvar+1)
    mem[initvar] = _RAND_0[7:0];
`endif // RANDOMIZE_MEM_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
