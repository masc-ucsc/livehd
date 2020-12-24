module Router(
  input         clock,
  input         reset,
  output        io_read_routing_table_request_ready,
  input         io_read_routing_table_request_valid,
  input  [31:0] io_read_routing_table_request_bits_addr,
  input         io_read_routing_table_response_ready,
  output        io_read_routing_table_response_valid,
  output [31:0] io_read_routing_table_response_bits,
  output        io_load_routing_table_request_ready,
  input         io_load_routing_table_request_valid,
  input  [31:0] io_load_routing_table_request_bits_addr,
  input  [31:0] io_load_routing_table_request_bits_data,
  output        io_in_ready,
  input         io_in_valid,
  input  [7:0]  io_in_bits_header,
  input  [63:0] io_in_bits_body,
  input         io_outs_0_ready,
  output        io_outs_0_valid,
  output [7:0]  io_outs_0_bits_header,
  output [63:0] io_outs_0_bits_body,
  input         io_outs_1_ready,
  output        io_outs_1_valid,
  output [7:0]  io_outs_1_bits_header,
  output [63:0] io_outs_1_bits_body,
  input         io_outs_2_ready,
  output        io_outs_2_valid,
  output [7:0]  io_outs_2_bits_header,
  output [63:0] io_outs_2_bits_body,
  input         io_outs_3_ready,
  output        io_outs_3_valid,
  output [7:0]  io_outs_3_bits_header,
  output [63:0] io_outs_3_bits_body
);
`ifdef RANDOMIZE_GARBAGE_ASSIGN
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_3;
`endif // RANDOMIZE_GARBAGE_ASSIGN
`ifdef RANDOMIZE_MEM_INIT
  reg [31:0] _RAND_0;
`endif // RANDOMIZE_MEM_INIT
  reg [2:0] tbl [0:14]; // @[Router.scala 51:18]
  wire [2:0] tbl_MPORT_data; // @[Router.scala 51:18]
  wire [3:0] tbl_MPORT_addr; // @[Router.scala 51:18]
  wire [2:0] tbl_idx_data; // @[Router.scala 51:18]
  wire [3:0] tbl_idx_addr; // @[Router.scala 51:18]
  wire [2:0] tbl_MPORT_2_data; // @[Router.scala 51:18]
  wire [3:0] tbl_MPORT_2_addr; // @[Router.scala 51:18]
  wire [2:0] tbl_MPORT_1_data; // @[Router.scala 51:18]
  wire [3:0] tbl_MPORT_1_addr; // @[Router.scala 51:18]
  wire  tbl_MPORT_1_mask; // @[Router.scala 51:18]
  wire  tbl_MPORT_1_en; // @[Router.scala 51:18]
  wire  _T = io_read_routing_table_request_valid & io_read_routing_table_response_ready; // @[Router.scala 65:44]
  wire  _T_4 = ~reset; // @[Router.scala 73:11]
  wire  _GEN_4 = 2'h1 == tbl_idx_data[1:0] ? io_outs_1_ready : io_outs_0_ready; // @[Router.scala 78:30 Router.scala 78:30]
  wire  _GEN_8 = 2'h2 == tbl_idx_data[1:0] ? io_outs_2_ready : _GEN_4; // @[Router.scala 78:30 Router.scala 78:30]
  wire  _GEN_12 = 2'h3 == tbl_idx_data[1:0] ? io_outs_3_ready : _GEN_8; // @[Router.scala 78:30 Router.scala 78:30]
  wire  _GEN_16 = 2'h0 == tbl_idx_data[1:0]; // @[Decoupled.scala 47:20 Decoupled.scala 47:20 Decoupled.scala 56:20]
  wire  _GEN_17 = 2'h1 == tbl_idx_data[1:0]; // @[Decoupled.scala 47:20 Decoupled.scala 47:20 Decoupled.scala 56:20]
  wire  _GEN_18 = 2'h2 == tbl_idx_data[1:0]; // @[Decoupled.scala 47:20 Decoupled.scala 47:20 Decoupled.scala 56:20]
  wire  _GEN_19 = 2'h3 == tbl_idx_data[1:0]; // @[Decoupled.scala 47:20 Decoupled.scala 47:20 Decoupled.scala 56:20]
  wire  _GEN_29 = _GEN_12 & _GEN_16; // @[Router.scala 78:30 Decoupled.scala 56:20]
  wire  _GEN_30 = _GEN_12 & _GEN_17; // @[Router.scala 78:30 Decoupled.scala 56:20]
  wire  _GEN_31 = _GEN_12 & _GEN_18; // @[Router.scala 78:30 Decoupled.scala 56:20]
  wire  _GEN_32 = _GEN_12 & _GEN_19; // @[Router.scala 78:30 Decoupled.scala 56:20]
  wire  _GEN_46 = io_in_valid & _GEN_12; // @[Router.scala 75:26 Decoupled.scala 72:20]
  wire  _GEN_47 = io_in_valid & _GEN_29; // @[Router.scala 75:26 Decoupled.scala 56:20]
  wire  _GEN_48 = io_in_valid & _GEN_30; // @[Router.scala 75:26 Decoupled.scala 56:20]
  wire  _GEN_49 = io_in_valid & _GEN_31; // @[Router.scala 75:26 Decoupled.scala 56:20]
  wire  _GEN_50 = io_in_valid & _GEN_32; // @[Router.scala 75:26 Decoupled.scala 56:20]
  wire  _GEN_68 = io_load_routing_table_request_valid ? 1'h0 : io_in_valid; // @[Router.scala 70:50 Router.scala 51:18]
  wire  _GEN_69 = io_load_routing_table_request_valid ? 1'h0 : _GEN_46; // @[Router.scala 70:50 Decoupled.scala 72:20]
  wire  _GEN_70 = io_load_routing_table_request_valid ? 1'h0 : _GEN_47; // @[Router.scala 70:50 Decoupled.scala 56:20]
  wire  _GEN_71 = io_load_routing_table_request_valid ? 1'h0 : _GEN_48; // @[Router.scala 70:50 Decoupled.scala 56:20]
  wire  _GEN_72 = io_load_routing_table_request_valid ? 1'h0 : _GEN_49; // @[Router.scala 70:50 Decoupled.scala 56:20]
  wire  _GEN_73 = io_load_routing_table_request_valid ? 1'h0 : _GEN_50; // @[Router.scala 70:50 Decoupled.scala 56:20]
  wire [2:0] _GEN_87 = io_read_routing_table_request_valid & io_read_routing_table_response_ready ? tbl_MPORT_data : 3'h0
    ; // @[Router.scala 65:85 Decoupled.scala 48:19 Router.scala 57:39]
  wire  _GEN_112 = ~_T; // @[Router.scala 73:11]
  assign tbl_MPORT_addr = io_read_routing_table_request_bits_addr[3:0];
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign tbl_MPORT_data = tbl[tbl_MPORT_addr]; // @[Router.scala 51:18]
  `else
  assign tbl_MPORT_data = tbl_MPORT_addr >= 4'hf ? _RAND_1[2:0] : tbl[tbl_MPORT_addr]; // @[Router.scala 51:18]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign tbl_idx_addr = io_in_bits_header[3:0];
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign tbl_idx_data = tbl[tbl_idx_addr]; // @[Router.scala 51:18]
  `else
  assign tbl_idx_data = tbl_idx_addr >= 4'hf ? _RAND_2[2:0] : tbl[tbl_idx_addr]; // @[Router.scala 51:18]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign tbl_MPORT_2_addr = io_in_bits_header[3:0];
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign tbl_MPORT_2_data = tbl[tbl_MPORT_2_addr]; // @[Router.scala 51:18]
  `else
  assign tbl_MPORT_2_data = tbl_MPORT_2_addr >= 4'hf ? _RAND_3[2:0] : tbl[tbl_MPORT_2_addr]; // @[Router.scala 51:18]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign tbl_MPORT_1_data = io_load_routing_table_request_bits_data[2:0];
  assign tbl_MPORT_1_addr = io_load_routing_table_request_bits_addr[3:0];
  assign tbl_MPORT_1_mask = 1'h1;
  assign tbl_MPORT_1_en = _T ? 1'h0 : io_load_routing_table_request_valid;
  assign io_read_routing_table_request_ready = io_read_routing_table_request_valid &
    io_read_routing_table_response_ready; // @[Router.scala 65:44]
  assign io_read_routing_table_response_valid = io_read_routing_table_request_valid &
    io_read_routing_table_response_ready; // @[Router.scala 65:44]
  assign io_read_routing_table_response_bits = {{29'd0}, _GEN_87}; // @[Router.scala 65:85 Decoupled.scala 48:19 Router.scala 57:39]
  assign io_load_routing_table_request_ready = io_read_routing_table_request_valid &
    io_read_routing_table_response_ready ? 1'h0 : io_load_routing_table_request_valid; // @[Router.scala 65:85 Decoupled.scala 72:20]
  assign io_in_ready = io_read_routing_table_request_valid & io_read_routing_table_response_ready ? 1'h0 : _GEN_69; // @[Router.scala 65:85 Decoupled.scala 72:20]
  assign io_outs_0_valid = io_read_routing_table_request_valid & io_read_routing_table_response_ready ? 1'h0 : _GEN_70; // @[Router.scala 65:85 Decoupled.scala 56:20]
  assign io_outs_0_bits_header = io_in_bits_header; // @[Decoupled.scala 48:19 Decoupled.scala 48:19]
  assign io_outs_0_bits_body = io_in_bits_body; // @[Decoupled.scala 48:19 Decoupled.scala 48:19]
  assign io_outs_1_valid = io_read_routing_table_request_valid & io_read_routing_table_response_ready ? 1'h0 : _GEN_71; // @[Router.scala 65:85 Decoupled.scala 56:20]
  assign io_outs_1_bits_header = io_in_bits_header; // @[Decoupled.scala 48:19 Decoupled.scala 48:19]
  assign io_outs_1_bits_body = io_in_bits_body; // @[Decoupled.scala 48:19 Decoupled.scala 48:19]
  assign io_outs_2_valid = io_read_routing_table_request_valid & io_read_routing_table_response_ready ? 1'h0 : _GEN_72; // @[Router.scala 65:85 Decoupled.scala 56:20]
  assign io_outs_2_bits_header = io_in_bits_header; // @[Decoupled.scala 48:19 Decoupled.scala 48:19]
  assign io_outs_2_bits_body = io_in_bits_body; // @[Decoupled.scala 48:19 Decoupled.scala 48:19]
  assign io_outs_3_valid = io_read_routing_table_request_valid & io_read_routing_table_response_ready ? 1'h0 : _GEN_73; // @[Router.scala 65:85 Decoupled.scala 56:20]
  assign io_outs_3_bits_header = io_in_bits_header; // @[Decoupled.scala 48:19 Decoupled.scala 48:19]
  assign io_outs_3_bits_body = io_in_bits_body; // @[Decoupled.scala 48:19 Decoupled.scala 48:19]
  always @(posedge clock) begin
    if(tbl_MPORT_1_en & tbl_MPORT_1_mask) begin
      tbl[tbl_MPORT_1_addr] <= tbl_MPORT_1_data; // @[Router.scala 51:18]
    end
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (~_T & io_load_routing_table_request_valid & ~reset) begin
          $fwrite(32'h80000002,"setting tbl(%d) to %d\n",io_load_routing_table_request_bits_addr,
            io_load_routing_table_request_bits_data); // @[Router.scala 73:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (_GEN_112 & ~io_load_routing_table_request_valid & io_in_valid & _GEN_12 & _T_4) begin
          $fwrite(32'h80000002,"got packet to route header %d, data %d, being routed to out(%d)\n",io_in_bits_header,
            io_in_bits_body,tbl_MPORT_2_data); // @[Router.scala 81:13]
        end
    `ifdef PRINTF_COND
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
  _RAND_1 = {1{`RANDOM}};
  _RAND_2 = {1{`RANDOM}};
  _RAND_3 = {1{`RANDOM}};
`endif // RANDOMIZE_GARBAGE_ASSIGN
`ifdef RANDOMIZE_MEM_INIT
  _RAND_0 = {1{`RANDOM}};
  for (initvar = 0; initvar < 15; initvar = initvar+1)
    tbl[initvar] = _RAND_0[2:0];
`endif // RANDOMIZE_MEM_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
