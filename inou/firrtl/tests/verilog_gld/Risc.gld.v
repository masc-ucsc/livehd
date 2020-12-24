module Risc(
  input         clock,
  input         reset,
  input         io_isWr,
  input  [7:0]  io_wrAddr,
  input  [31:0] io_wrData,
  input         io_boot,
  output        io_valid,
  output [31:0] io_out
);
`ifdef RANDOMIZE_MEM_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_2;
`endif // RANDOMIZE_REG_INIT
  reg [31:0] file [0:255]; // @[Risc.scala 16:17]
  wire [31:0] file__T_1_data; // @[Risc.scala 16:17]
  wire [7:0] file__T_1_addr; // @[Risc.scala 16:17]
  wire [31:0] file__T_3_data; // @[Risc.scala 16:17]
  wire [7:0] file__T_3_addr; // @[Risc.scala 16:17]
  wire [31:0] file__T_12_data; // @[Risc.scala 16:17]
  wire [7:0] file__T_12_addr; // @[Risc.scala 16:17]
  wire  file__T_12_mask; // @[Risc.scala 16:17]
  wire  file__T_12_en; // @[Risc.scala 16:17]
  reg [31:0] code [0:255]; // @[Risc.scala 17:17]
  wire [31:0] code_inst_data; // @[Risc.scala 17:17]
  wire [7:0] code_inst_addr; // @[Risc.scala 17:17]
  wire [31:0] code__T_4_data; // @[Risc.scala 17:17]
  wire [7:0] code__T_4_addr; // @[Risc.scala 17:17]
  wire  code__T_4_mask; // @[Risc.scala 17:17]
  wire  code__T_4_en; // @[Risc.scala 17:17]
  reg [7:0] pc; // @[Risc.scala 18:21]
  wire [7:0] op = code_inst_data[31:24]; // @[Risc.scala 23:18]
  wire [7:0] rci = code_inst_data[23:16]; // @[Risc.scala 24:18]
  wire [7:0] rai = code_inst_data[15:8]; // @[Risc.scala 25:18]
  wire [7:0] rbi = code_inst_data[7:0]; // @[Risc.scala 26:18]
  wire [31:0] ra = rai == 8'h0 ? 32'h0 : file__T_1_data; // @[Risc.scala 28:15]
  wire [31:0] rb = rbi == 8'h0 ? 32'h0 : file__T_3_data; // @[Risc.scala 29:15]
  wire  _T_5 = 8'h0 == op; // @[Conditional.scala 37:30]
  wire [31:0] _T_7 = ra + rb; // @[Risc.scala 42:29]
  wire  _T_8 = 8'h1 == op; // @[Conditional.scala 37:30]
  wire [15:0] _GEN_31 = {rai, 8'h0}; // @[Risc.scala 43:31]
  wire [22:0] _T_9 = {{7'd0}, _GEN_31}; // @[Risc.scala 43:31]
  wire [22:0] _GEN_32 = {{15'd0}, rbi}; // @[Risc.scala 43:39]
  wire [22:0] _T_10 = _T_9 | _GEN_32; // @[Risc.scala 43:39]
  wire [22:0] _GEN_0 = _T_8 ? _T_10 : 23'h0; // @[Conditional.scala 39:67 Risc.scala 43:23 Risc.scala 34:12]
  wire [31:0] _GEN_1 = _T_5 ? _T_7 : {{9'd0}, _GEN_0}; // @[Conditional.scala 40:58 Risc.scala 42:23]
  wire  _T_11 = rci == 8'hff; // @[Risc.scala 46:15]
  wire  _GEN_5 = rci == 8'hff ? 1'h0 : 1'h1; // @[Risc.scala 46:26 Risc.scala 16:17 Risc.scala 49:11]
  wire [31:0] _GEN_9 = io_boot ? 32'h0 : _GEN_1; // @[Risc.scala 38:25 Risc.scala 34:12]
  wire [31:0] rc = io_isWr ? 32'h0 : _GEN_9; // @[Risc.scala 36:18 Risc.scala 34:12]
  wire [7:0] _T_14 = pc + 8'h1; // @[Risc.scala 51:14]
  wire [31:0] _GEN_10 = io_boot ? 32'h0 : rc; // @[Risc.scala 38:25 Risc.scala 33:12 Risc.scala 45:12]
  wire  _GEN_11 = io_boot ? 1'h0 : _T_11; // @[Risc.scala 38:25 Risc.scala 32:12]
  wire  _GEN_14 = io_boot ? 1'h0 : _GEN_5; // @[Risc.scala 38:25 Risc.scala 16:17]
  assign file__T_1_addr = code_inst_data[15:8];
  assign file__T_1_data = file[file__T_1_addr]; // @[Risc.scala 16:17]
  assign file__T_3_addr = code_inst_data[7:0];
  assign file__T_3_data = file[file__T_3_addr]; // @[Risc.scala 16:17]
  assign file__T_12_data = io_isWr ? 32'h0 : _GEN_9;
  assign file__T_12_addr = code_inst_data[23:16];
  assign file__T_12_mask = 1'h1;
  assign file__T_12_en = io_isWr ? 1'h0 : _GEN_14;
  assign code_inst_addr = pc;
  assign code_inst_data = code[code_inst_addr]; // @[Risc.scala 17:17]
  assign code__T_4_data = io_wrData;
  assign code__T_4_addr = io_wrAddr;
  assign code__T_4_mask = 1'h1;
  assign code__T_4_en = io_isWr;
  assign io_valid = io_isWr ? 1'h0 : _GEN_11; // @[Risc.scala 36:18 Risc.scala 32:12]
  assign io_out = io_isWr ? 32'h0 : _GEN_10; // @[Risc.scala 36:18 Risc.scala 33:12]
  always @(posedge clock) begin
    if(file__T_12_en & file__T_12_mask) begin
      file[file__T_12_addr] <= file__T_12_data; // @[Risc.scala 16:17]
    end
    if(code__T_4_en & code__T_4_mask) begin
      code[code__T_4_addr] <= code__T_4_data; // @[Risc.scala 17:17]
    end
    if (reset) begin // @[Risc.scala 18:21]
      pc <= 8'h0; // @[Risc.scala 18:21]
    end else if (!(io_isWr)) begin // @[Risc.scala 36:18]
      if (io_boot) begin // @[Risc.scala 38:25]
        pc <= 8'h0; // @[Risc.scala 39:8]
      end else begin
        pc <= _T_14; // @[Risc.scala 51:8]
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
  for (initvar = 0; initvar < 256; initvar = initvar+1)
    file[initvar] = _RAND_0[31:0];
  _RAND_1 = {1{`RANDOM}};
  for (initvar = 0; initvar < 256; initvar = initvar+1)
    code[initvar] = _RAND_1[31:0];
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  _RAND_2 = {1{`RANDOM}};
  pc = _RAND_2[7:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
