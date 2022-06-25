module RegisterFileSynthesizable(
  input         clock,
  input         reset,
  input  [5:0]  io_read_ports_0_addr,
  output [64:0] io_read_ports_0_data,
  input  [5:0]  io_read_ports_1_addr,
  output [64:0] io_read_ports_1_data,
  input  [5:0]  io_read_ports_2_addr,
  output [64:0] io_read_ports_2_data,
  input         io_write_ports_0_valid,
  input  [5:0]  io_write_ports_0_bits_addr,
  input  [64:0] io_write_ports_0_bits_data,
  input         io_write_ports_1_valid,
  input  [5:0]  io_write_ports_1_bits_addr,
  input  [64:0] io_write_ports_1_bits_data
);
`ifdef RANDOMIZE_GARBAGE_ASSIGN
  reg [95:0] _RAND_1;
  reg [95:0] _RAND_2;
  reg [95:0] _RAND_3;
`endif // RANDOMIZE_GARBAGE_ASSIGN
`ifdef RANDOMIZE_MEM_INIT
  reg [95:0] _RAND_0;
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_4;
  reg [31:0] _RAND_5;
  reg [31:0] _RAND_6;
`endif // RANDOMIZE_REG_INIT
  reg [64:0] regfile [0:47]; // @[regfile.scala 117:20]
  wire  regfile__T_2_en; // @[regfile.scala 117:20]
  wire [5:0] regfile__T_2_addr; // @[regfile.scala 117:20]
  wire [64:0] regfile__T_2_data; // @[regfile.scala 117:20]
  wire  regfile__T_5_en; // @[regfile.scala 117:20]
  wire [5:0] regfile__T_5_addr; // @[regfile.scala 117:20]
  wire [64:0] regfile__T_5_data; // @[regfile.scala 117:20]
  wire  regfile__T_8_en; // @[regfile.scala 117:20]
  wire [5:0] regfile__T_8_addr; // @[regfile.scala 117:20]
  wire [64:0] regfile__T_8_data; // @[regfile.scala 117:20]
  wire [64:0] regfile__T_9_data; // @[regfile.scala 117:20]
  wire [5:0] regfile__T_9_addr; // @[regfile.scala 117:20]
  wire  regfile__T_9_mask; // @[regfile.scala 117:20]
  wire  regfile__T_9_en; // @[regfile.scala 117:20]
  wire [64:0] regfile__T_10_data; // @[regfile.scala 117:20]
  wire [5:0] regfile__T_10_addr; // @[regfile.scala 117:20]
  wire  regfile__T_10_mask; // @[regfile.scala 117:20]
  wire  regfile__T_10_en; // @[regfile.scala 117:20]
  reg [5:0] read_addrs_0; // @[regfile.scala 125:50]
  reg [5:0] read_addrs_1; // @[regfile.scala 125:50]
  reg [5:0] read_addrs_2; // @[regfile.scala 125:50]
  wire  _T_12 = ~io_write_ports_1_valid; // @[regfile.scala 172:16]
  wire  _T_13 = ~io_write_ports_0_valid | _T_12; // @[regfile.scala 171:41]
  wire  _T_14 = io_write_ports_0_bits_addr != io_write_ports_1_bits_addr; // @[regfile.scala 173:45]
  wire  _T_15 = _T_13 | _T_14; // @[regfile.scala 172:41]
  wire  _T_16 = io_write_ports_0_bits_addr == 6'h0; // @[regfile.scala 174:45]
  wire  _T_17 = _T_15 | _T_16; // @[regfile.scala 173:78]
  assign regfile__T_2_en = 1'h1;
  assign regfile__T_2_addr = read_addrs_0;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign regfile__T_2_data = regfile[regfile__T_2_addr]; // @[regfile.scala 117:20]
  `else
  assign regfile__T_2_data = regfile__T_2_addr >= 6'h30 ? _RAND_1[64:0] : regfile[regfile__T_2_addr]; // @[regfile.scala 117:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign regfile__T_5_en = 1'h1;
  assign regfile__T_5_addr = read_addrs_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign regfile__T_5_data = regfile[regfile__T_5_addr]; // @[regfile.scala 117:20]
  `else
  assign regfile__T_5_data = regfile__T_5_addr >= 6'h30 ? _RAND_2[64:0] : regfile[regfile__T_5_addr]; // @[regfile.scala 117:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign regfile__T_8_en = 1'h1;
  assign regfile__T_8_addr = read_addrs_2;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign regfile__T_8_data = regfile[regfile__T_8_addr]; // @[regfile.scala 117:20]
  `else
  assign regfile__T_8_data = regfile__T_8_addr >= 6'h30 ? _RAND_3[64:0] : regfile[regfile__T_8_addr]; // @[regfile.scala 117:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign regfile__T_9_data = io_write_ports_0_bits_data;
  assign regfile__T_9_addr = io_write_ports_0_bits_addr;
  assign regfile__T_9_mask = 1'h1;
  assign regfile__T_9_en = io_write_ports_0_valid;
  assign regfile__T_10_data = io_write_ports_1_bits_data;
  assign regfile__T_10_addr = io_write_ports_1_bits_addr;
  assign regfile__T_10_mask = 1'h1;
  assign regfile__T_10_en = io_write_ports_1_valid;
  assign io_read_ports_0_data = regfile__T_2_data; // @[regfile.scala 122:23 128:18]
  assign io_read_ports_1_data = regfile__T_5_data; // @[regfile.scala 122:23 128:18]
  assign io_read_ports_2_data = regfile__T_8_data; // @[regfile.scala 122:23 128:18]
  always @(posedge clock) begin
    if (regfile__T_9_en & regfile__T_9_mask) begin
      regfile[regfile__T_9_addr] <= regfile__T_9_data; // @[regfile.scala 117:20]
    end
    if (regfile__T_10_en & regfile__T_10_mask) begin
      regfile[regfile__T_10_addr] <= regfile__T_10_data; // @[regfile.scala 117:20]
    end
    read_addrs_0 <= io_read_ports_0_addr; // @[regfile.scala 125:50]
    read_addrs_1 <= io_read_ports_1_addr; // @[regfile.scala 125:50]
    read_addrs_2 <= io_read_ports_2_addr; // @[regfile.scala 125:50]
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (~(_T_17 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: [regfile] too many writers a register\n    at regfile.scala:171 assert(!io.write_ports(i).valid ||\n"
            ); // @[regfile.scala 171:15]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (~(_T_17 | reset)) begin
          $fatal; // @[regfile.scala 171:15]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
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
`ifdef RANDOMIZE_GARBAGE_ASSIGN
  _RAND_1 = {3{`RANDOM}};
  _RAND_2 = {3{`RANDOM}};
  _RAND_3 = {3{`RANDOM}};
`endif // RANDOMIZE_GARBAGE_ASSIGN
`ifdef RANDOMIZE_MEM_INIT
  _RAND_0 = {3{`RANDOM}};
  for (initvar = 0; initvar < 48; initvar = initvar+1)
    regfile[initvar] = _RAND_0[64:0];
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  _RAND_4 = {1{`RANDOM}};
  read_addrs_0 = _RAND_4[5:0];
  _RAND_5 = {1{`RANDOM}};
  read_addrs_1 = _RAND_5[5:0];
  _RAND_6 = {1{`RANDOM}};
  read_addrs_2 = _RAND_6[5:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
