module decode_block_data(
    input clk,
    input [64-1:0] reg_addr__4664,
    input [5-1:0] ra1__4977,
    input [5-1:0] ra1__5302,
    input [5-1:0] ra1__5507,
    input [5-1:0] ra2__5027,
    input [64-1:0] __tid17235,
    output [64-1:0] __tid16150__17356,
    output [64-1:0] __tid16170__17358,
    output [64-1:0] __tid16405__17360,
    output [64-1:0] __tid16415__17363,
    output [64-1:0] __tid16426__17366,
    output [64-1:0] __tid16462__17369,
    output [64-1:0] __tid16485__17372);

  logic [64-1:0] data[32-1:0] /*verilator public*/;
  always @(posedge clk) begin
     data[reg_addr__4664[4:0]] <= __tid17235;
  end

  assign __tid16150__17356 = data[reg_addr__4664[4:0]];
  assign __tid16170__17358 = data[reg_addr__4664[4:0]];
  assign __tid16405__17360 = data[ra1__4977[4:0]];
  assign __tid16415__17363 = data[ra1__5302[4:0]];
  assign __tid16426__17366 = data[ra1__5507[4:0]];
  assign __tid16462__17369 = data[ra2__5027[4:0]];
  assign __tid16485__17372 = data[reg_addr__4664[4:0]];
endmodule