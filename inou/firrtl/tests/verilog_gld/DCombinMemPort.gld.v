module DCombinMemPort(
  input         clock,
  input         reset,
  input  [31:0] io_pipeline_address,
  input         io_pipeline_valid,
  output        io_pipeline_good,
  input  [31:0] io_pipeline_writedata,
  input         io_pipeline_memread,
  input         io_pipeline_memwrite,
  input  [1:0]  io_pipeline_maskmode,
  input         io_pipeline_sext,
  output [31:0] io_pipeline_readdata,
  input         io_bus_request_ready,
  output        io_bus_request_valid,
  output [31:0] io_bus_request_bits_address,
  output [31:0] io_bus_request_bits_writedata,
  output [1:0]  io_bus_request_bits_operation,
  input         io_bus_response_valid,
  input  [31:0] io_bus_response_bits_data
);
  wire [31:0] _T_16 = {io_bus_response_bits_data[31:8],io_pipeline_writedata[7:0]};
  wire [31:0] _T_22 = {io_bus_response_bits_data[31:16],io_pipeline_writedata[15:8],io_bus_response_bits_data[7:0]};
  wire [31:0] _T_28 = {io_bus_response_bits_data[31:24],io_pipeline_writedata[23:16],io_bus_response_bits_data[15:0]};
  wire [31:0] _T_31 = {io_pipeline_writedata[31:24],io_bus_response_bits_data[23:0]};
  wire [31:0] _GEN_4 = io_pipeline_address[1:0] == 2'h2 ? _T_28 : _T_31; // @[]
  wire [31:0] _GEN_5 = io_pipeline_address[1:0] == 2'h1 ? _T_22 : _GEN_4; // @[]
  wire [31:0] _GEN_6 = io_pipeline_address[1:0] == 2'h0 ? _T_16 : _GEN_5; // @[]
  wire [31:0] _T_35 = {io_bus_response_bits_data[31:16],io_pipeline_writedata[15:0]};
  wire [31:0] _T_38 = {io_pipeline_writedata[31:16],io_bus_response_bits_data[15:0]};
  wire [31:0] _GEN_7 = io_pipeline_address[1:0] == 2'h0 ? _T_35 : _T_38; // @[]
  wire [31:0] _GEN_8 = io_pipeline_maskmode == 2'h0 ? _GEN_6 : _GEN_7; // @[]
  wire [5:0] _T_43 = io_pipeline_address[1:0] * 4'h8;
  wire [31:0] _T_44 = io_bus_response_bits_data >> _T_43;
  wire [31:0] _T_45 = _T_44 & 32'hff;
  wire [31:0] _T_49 = _T_44 & 32'hffff;
  wire [31:0] _GEN_10 = io_pipeline_maskmode == 2'h1 ? _T_49 : io_bus_response_bits_data; // @[]
  wire [31:0] _GEN_11 = io_pipeline_maskmode == 2'h0 ? _T_45 : _GEN_10; // @[]
  wire [23:0] _T_53 = _GEN_11[7] ? 24'hffffff : 24'h0;
  wire [31:0] _T_55 = {_T_53,_GEN_11[7:0]};
  wire [15:0] _T_59 = _GEN_11[15] ? 16'hffff : 16'h0;
  wire [31:0] _T_61 = {_T_59,_GEN_11[15:0]};
  wire [31:0] _GEN_12 = io_pipeline_maskmode == 2'h1 ? _T_61 : _GEN_11; // @[]
  wire [31:0] _GEN_13 = io_pipeline_maskmode == 2'h0 ? _T_55 : _GEN_12; // @[]
  wire [31:0] _GEN_14 = io_pipeline_sext ? _GEN_13 : _GEN_11; // @[]
  wire [31:0] _GEN_15 = io_pipeline_memread ? _GEN_14 : 32'h0; // @[]
  wire [31:0] _GEN_17 = io_pipeline_memwrite ? 32'h0 : _GEN_15; // @[]
  assign io_pipeline_good = 1'h1;
  assign io_pipeline_readdata = io_bus_response_valid ? _GEN_17 : 32'h0; // @[]
  assign io_bus_request_valid = io_pipeline_valid & (io_pipeline_memread | io_pipeline_memwrite); // @[]
  assign io_bus_request_bits_address = io_pipeline_address; // @[]
  assign io_bus_request_bits_writedata = io_pipeline_maskmode != 2'h2 ? _GEN_8 : io_pipeline_writedata; // @[]
  assign io_bus_request_bits_operation = io_pipeline_memwrite ? 2'h2 : 2'h0; // @[]
  always @(posedge clock) begin
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_pipeline_valid & (io_pipeline_memread | io_pipeline_memwrite) & ~(~(io_pipeline_memread &
          io_pipeline_memwrite) | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed\n    at memory-combin-ports.scala:46 assert(!(io.pipeline.memread && io.pipeline.memwrite))\n"
            );
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_pipeline_valid & (io_pipeline_memread | io_pipeline_memwrite) & ~(~(io_pipeline_memread &
          io_pipeline_memwrite) | reset)) begin
          $fatal;
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
  end
endmodule
