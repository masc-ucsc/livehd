module DualPortedCombinMemory(
  input         clock,
  input         reset,
  output        io_imem_request_ready,
  input         io_imem_request_valid,
  input  [31:0] io_imem_request_bits_address,
  input  [31:0] io_imem_request_bits_writedata,
  input  [1:0]  io_imem_request_bits_operation,
  output        io_imem_response_valid,
  output [31:0] io_imem_response_bits_data,
  output        io_dmem_request_ready,
  input         io_dmem_request_valid,
  input  [31:0] io_dmem_request_bits_address,
  input  [31:0] io_dmem_request_bits_writedata,
  input  [1:0]  io_dmem_request_bits_operation,
  output        io_dmem_response_valid,
  output [31:0] io_dmem_response_bits_data
);
`ifdef RANDOMIZE_MEM_INIT
  reg [31:0] _RAND_0;
`endif // RANDOMIZE_MEM_INIT
  reg [31:0] memory [0:16383];
  wire  memory__T_9_en;
  wire [13:0] memory__T_9_addr;
  wire [31:0] memory__T_9_data;
  wire  memory__T_20_en;
  wire [13:0] memory__T_20_addr;
  wire [31:0] memory__T_20_data;
  wire [31:0] memory__T_24_data;
  wire [13:0] memory__T_24_addr;
  wire  memory__T_24_mask;
  wire  memory__T_24_en;
  wire [31:0] _GEN_3 = io_imem_request_bits_address < 32'h10000 ? memory__T_9_data : 32'h0; // @[]
  assign memory__T_9_en = io_imem_request_valid & io_imem_request_bits_address < 32'h10000;
  assign memory__T_9_addr = io_imem_request_bits_address[15:2];
  assign memory__T_9_data = memory[memory__T_9_addr];
  assign memory__T_20_en = io_dmem_request_valid;
  assign memory__T_20_addr = io_dmem_request_bits_address[15:2];
  assign memory__T_20_data = memory[memory__T_20_addr];
  assign memory__T_24_data = io_dmem_request_bits_writedata;
  assign memory__T_24_addr = io_dmem_request_bits_address[15:2];
  assign memory__T_24_mask = 1'h1;
  assign memory__T_24_en = io_dmem_request_valid & io_dmem_request_bits_operation == 2'h2;
  assign io_imem_request_ready = 1'h1;
  assign io_imem_response_valid = io_imem_request_valid & io_imem_request_bits_address < 32'h10000; // @[]
  assign io_imem_response_bits_data = io_imem_request_valid ? _GEN_3 : 32'h0; // @[]
  assign io_dmem_request_ready = 1'h1;
  assign io_dmem_response_valid = io_dmem_request_valid; // @[]
  assign io_dmem_response_bits_data = io_dmem_request_valid ? memory__T_20_data : 32'h0; // @[]
  always @(posedge clock) begin
    if (memory__T_24_en & memory__T_24_mask) begin
      memory[memory__T_24_addr] <= memory__T_24_data;
    end
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_imem_request_valid & ~(io_imem_request_bits_operation == 2'h0 | reset)) begin
          $fwrite(32'h80000002,"Assertion failed\n    at memory.scala:30 assert(request.operation === Read)\n");
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_imem_request_valid & ~(io_imem_request_bits_operation == 2'h0 | reset)) begin
          $fatal;
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_dmem_request_valid & ~(io_dmem_request_bits_operation != 2'h1 | reset)) begin
          $fwrite(32'h80000002,"Assertion failed\n    at memory.scala:56 assert (request.operation =/= Write)\n");
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_dmem_request_valid & ~(io_dmem_request_bits_operation != 2'h1 | reset)) begin
          $fatal;
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_dmem_request_valid & ~(io_dmem_request_bits_address < 32'h10000 | reset)) begin
          $fwrite(32'h80000002,"Assertion failed\n    at memory.scala:58 assert (request.address < size.U)\n");
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_dmem_request_valid & ~(io_dmem_request_bits_address < 32'h10000 | reset)) begin
          $fatal;
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
`ifdef RANDOMIZE_MEM_INIT
  _RAND_0 = {1{`RANDOM}};
  for (initvar = 0; initvar < 16384; initvar = initvar+1)
    memory[initvar] = _RAND_0[31:0];
`endif // RANDOMIZE_MEM_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
