/*
The mapped netlist: `\tile.mem ` is a bit-blasted memory module whose storage
is eight QN-output DFF cells `\_mem[i][b] ` with the inlined cell model (state
`flop_16` holds ~D, QN = flop_16, and the netlist drives D = ~next so the state
IS the stored bit). Each state cut reads `tile.mem._mem[i][b].flop_16`.
*/
module DFFHQN_T(input D, input CLK, output reg QN);
reg flop_16;
reg ___next_flop_16;
always_comb begin
  QN = flop_16;
  ___next_flop_16 = (~D);
end
always @(posedge CLK) begin
  flop_16 <= ___next_flop_16;
end
endmodule

module cgen_memory_1rd_1wr_0123456789abcdef_blasted(
   input clk
  ,input [1:0] wr_addr_0
  ,input [1:0] wr_din_0
  ,input wr_enable_0
  ,input [1:0] rd_addr_0
  ,output reg [1:0] rd_dout_0
);
wire \_mem[0][0]_3 ;
wire \_mem[0][1]_3 ;
wire \_mem[1][0]_3 ;
wire \_mem[1][1]_3 ;
wire \_mem[2][0]_3 ;
wire \_mem[2][1]_3 ;
wire \_mem[3][0]_3 ;
wire \_mem[3][1]_3 ;
wire we_0 = wr_enable_0 & (wr_addr_0 == 2'd0);
DFFHQN_T \_mem[0][0] (.D(~(we_0 ? wr_din_0[0] : \_mem[0][0]_3 )), .CLK(clk), .QN(\_mem[0][0]_3 ));
DFFHQN_T \_mem[0][1] (.D(~(we_0 ? wr_din_0[1] : \_mem[0][1]_3 )), .CLK(clk), .QN(\_mem[0][1]_3 ));
wire we_1 = wr_enable_0 & (wr_addr_0 == 2'd1);
DFFHQN_T \_mem[1][0] (.D(~(we_1 ? wr_din_0[0] : \_mem[1][0]_3 )), .CLK(clk), .QN(\_mem[1][0]_3 ));
DFFHQN_T \_mem[1][1] (.D(~(we_1 ? wr_din_0[1] : \_mem[1][1]_3 )), .CLK(clk), .QN(\_mem[1][1]_3 ));
wire we_2 = wr_enable_0 & (wr_addr_0 == 2'd2);
DFFHQN_T \_mem[2][0] (.D(~(we_2 ? wr_din_0[0] : \_mem[2][0]_3 )), .CLK(clk), .QN(\_mem[2][0]_3 ));
DFFHQN_T \_mem[2][1] (.D(~(we_2 ? wr_din_0[1] : \_mem[2][1]_3 )), .CLK(clk), .QN(\_mem[2][1]_3 ));
wire we_3 = wr_enable_0 & (wr_addr_0 == 2'd3);
DFFHQN_T \_mem[3][0] (.D(~(we_3 ? wr_din_0[0] : \_mem[3][0]_3 )), .CLK(clk), .QN(\_mem[3][0]_3 ));
DFFHQN_T \_mem[3][1] (.D(~(we_3 ? wr_din_0[1] : \_mem[3][1]_3 )), .CLK(clk), .QN(\_mem[3][1]_3 ));
always_comb begin
  case (rd_addr_0)
    2'd0: rd_dout_0 = {\_mem[0][1]_3 , \_mem[0][0]_3 };
    2'd1: rd_dout_0 = {\_mem[1][1]_3 , \_mem[1][0]_3 };
    2'd2: rd_dout_0 = {\_mem[2][1]_3 , \_mem[2][0]_3 };
    2'd3: rd_dout_0 = {\_mem[3][1]_3 , \_mem[3][0]_3 };
    default: rd_dout_0 = 2'd0;
  endcase
end
endmodule

module mem_bank_cell(
  input  logic       clk,
  input  logic       wr_valid,
  input  logic [1:0] wr_addr,
  input  logic [1:0] wr_data,
  input  logic [1:0] rd_addr,
  output logic [1:0] rd_data
);
  cgen_memory_1rd_1wr_0123456789abcdef_blasted \tile.mem (
    .clk(clk), .wr_addr_0(wr_addr), .wr_din_0(wr_data), .wr_enable_0(wr_valid),
    .rd_addr_0(rd_addr), .rd_dout_0(rd_data));
endmodule
