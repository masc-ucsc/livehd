module SmemStruct(
  input        clock,
  input        reset,
  input        io_en,
  input        io_write,
  input  [9:0] io_addr,
  input  [1:0] io_din_aa_d1,
  input  [1:0] io_din_aa_d2,
  input  [3:0] io_din_bb,
  output [1:0] io_dout_aa_d1,
  output [1:0] io_dout_aa_d2,
  output [3:0] io_dout_bb
);
`ifdef RANDOMIZE_MEM_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_3;
  reg [31:0] _RAND_6;
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_4;
  reg [31:0] _RAND_5;
  reg [31:0] _RAND_7;
  reg [31:0] _RAND_8;
`endif // RANDOMIZE_REG_INIT
  reg [1:0] mem_aa_d1 [0:1023];
  wire  mem_aa_d1_mportr_en;
  wire [9:0] mem_aa_d1_mportr_addr;
  wire [1:0] mem_aa_d1_mportr_data;
  wire [1:0] mem_aa_d1_mportw_data;
  wire [9:0] mem_aa_d1_mportw_addr;
  wire  mem_aa_d1_mportw_mask;
  wire  mem_aa_d1_mportw_en;
  reg  mem_aa_d1_mportr_en_pipe_0;
  reg [9:0] mem_aa_d1_mportr_addr_pipe_0;
  reg [1:0] mem_aa_d2 [0:1023];
  wire  mem_aa_d2_mportr_en;
  wire [9:0] mem_aa_d2_mportr_addr;
  wire [1:0] mem_aa_d2_mportr_data;
  wire [1:0] mem_aa_d2_mportw_data;
  wire [9:0] mem_aa_d2_mportw_addr;
  wire  mem_aa_d2_mportw_mask;
  wire  mem_aa_d2_mportw_en;
  reg  mem_aa_d2_mportr_en_pipe_0;
  reg [9:0] mem_aa_d2_mportr_addr_pipe_0;
  reg [3:0] mem_bb [0:1023];
  wire  mem_bb_mportr_en;
  wire [9:0] mem_bb_mportr_addr;
  wire [3:0] mem_bb_mportr_data;
  wire [3:0] mem_bb_mportw_data;
  wire [9:0] mem_bb_mportw_addr;
  wire  mem_bb_mportw_mask;
  wire  mem_bb_mportw_en;
  reg  mem_bb_mportr_en_pipe_0;
  reg [9:0] mem_bb_mportr_addr_pipe_0;
  assign mem_aa_d1_mportr_en = mem_aa_d1_mportr_en_pipe_0;
  assign mem_aa_d1_mportr_addr = mem_aa_d1_mportr_addr_pipe_0;
  assign mem_aa_d1_mportr_data = mem_aa_d1[mem_aa_d1_mportr_addr];
  assign mem_aa_d1_mportw_data = io_din_aa_d1;
  assign mem_aa_d1_mportw_addr = io_addr;
  assign mem_aa_d1_mportw_mask = 1'h1;
  assign mem_aa_d1_mportw_en = 1'h1;
  assign mem_aa_d2_mportr_en = mem_aa_d2_mportr_en_pipe_0;
  assign mem_aa_d2_mportr_addr = mem_aa_d2_mportr_addr_pipe_0;
  assign mem_aa_d2_mportr_data = mem_aa_d2[mem_aa_d2_mportr_addr];
  assign mem_aa_d2_mportw_data = io_din_aa_d2;
  assign mem_aa_d2_mportw_addr = io_addr;
  assign mem_aa_d2_mportw_mask = 1'h1;
  assign mem_aa_d2_mportw_en = 1'h1;
  assign mem_bb_mportr_en = mem_bb_mportr_en_pipe_0;
  assign mem_bb_mportr_addr = mem_bb_mportr_addr_pipe_0;
  assign mem_bb_mportr_data = mem_bb[mem_bb_mportr_addr];
  assign mem_bb_mportw_data = io_din_bb;
  assign mem_bb_mportw_addr = io_addr;
  assign mem_bb_mportw_mask = 1'h1;
  assign mem_bb_mportw_en = 1'h1;
  assign io_dout_aa_d1 = mem_aa_d1_mportr_data;
  assign io_dout_aa_d2 = mem_aa_d2_mportr_data;
  assign io_dout_bb = mem_bb_mportr_data;
  always @(posedge clock) begin
    if (mem_aa_d1_mportw_en & mem_aa_d1_mportw_mask) begin
      mem_aa_d1[mem_aa_d1_mportw_addr] <= mem_aa_d1_mportw_data;
    end
    if (io_addr > 10'h20) begin
      mem_aa_d1_mportr_en_pipe_0 <= io_en;
    end else begin
      mem_aa_d1_mportr_en_pipe_0 <= 1'h1;
    end
    if (io_addr > 10'h20 ? io_en : 1'h1) begin
      mem_aa_d1_mportr_addr_pipe_0 <= io_addr;
    end
    if (mem_aa_d2_mportw_en & mem_aa_d2_mportw_mask) begin
      mem_aa_d2[mem_aa_d2_mportw_addr] <= mem_aa_d2_mportw_data;
    end
    if (io_addr > 10'h20) begin
      mem_aa_d2_mportr_en_pipe_0 <= io_en;
    end else begin
      mem_aa_d2_mportr_en_pipe_0 <= 1'h1;
    end
    if (io_addr > 10'h20 ? io_en : 1'h1) begin
      mem_aa_d2_mportr_addr_pipe_0 <= io_addr;
    end
    if (mem_bb_mportw_en & mem_bb_mportw_mask) begin
      mem_bb[mem_bb_mportw_addr] <= mem_bb_mportw_data;
    end
    if (io_addr > 10'h20) begin
      mem_bb_mportr_en_pipe_0 <= io_en;
    end else begin
      mem_bb_mportr_en_pipe_0 <= 1'h1;
    end
    if (io_addr > 10'h20 ? io_en : 1'h1) begin
      mem_bb_mportr_addr_pipe_0 <= io_addr;
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
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    mem_aa_d1[initvar] = _RAND_0[1:0];
  _RAND_3 = {1{`RANDOM}};
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    mem_aa_d2[initvar] = _RAND_3[1:0];
  _RAND_6 = {1{`RANDOM}};
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    mem_bb[initvar] = _RAND_6[3:0];
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  _RAND_1 = {1{`RANDOM}};
  mem_aa_d1_mportr_en_pipe_0 = _RAND_1[0:0];
  _RAND_2 = {1{`RANDOM}};
  mem_aa_d1_mportr_addr_pipe_0 = _RAND_2[9:0];
  _RAND_4 = {1{`RANDOM}};
  mem_aa_d2_mportr_en_pipe_0 = _RAND_4[0:0];
  _RAND_5 = {1{`RANDOM}};
  mem_aa_d2_mportr_addr_pipe_0 = _RAND_5[9:0];
  _RAND_7 = {1{`RANDOM}};
  mem_bb_mportr_en_pipe_0 = _RAND_7[0:0];
  _RAND_8 = {1{`RANDOM}};
  mem_bb_mportr_addr_pipe_0 = _RAND_8[9:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
